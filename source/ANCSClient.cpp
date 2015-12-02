/* mbed Microcontroller Library
 * Copyright (c) 2006-2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ble-ancs-client/ANCSClient.h"

#include "mbed-drivers/mbed.h"

// control debug output
#if 1
#include <stdio.h>
#define DEBUGOUT(...) { printf(__VA_ARGS__); }
#else
#define DEBUGOUT(...) /* nothing */
#endif // DEBUGOUT

/*****************************************************************************/
/* NRF51                                                                     */
/*****************************************************************************/
#include "ble_gap.h"

#define SEC_PARAM_BOND                  1                                                    /**< Perform bonding. */
#define SEC_PARAM_MITM                  0                                                    /**< Man In The Middle protection not required. */
#define SEC_PARAM_IO_CAPABILITIES       BLE_GAP_IO_CAPS_NONE                                 /**< No I/O capabilities. */
#define SEC_PARAM_OOB                   0                                                    /**< Out Of Band data not available. */
#define SEC_PARAM_MIN_KEY_SIZE          7                                                    /**< Minimum encryption key size. */
#define SEC_PARAM_MAX_KEY_SIZE          16                                                   /**< Maximum encryption key size. */

static ble_gap_sec_params_t             m_sec_params;

/*****************************************************************************/


/*****************************************************************************/
/* C to C++                                                                  */
/*****************************************************************************/

static ANCSClient* ancsBridge = NULL;

static void bridgeCharacteristicDiscoveryCallback(const DiscoveredCharacteristic* characteristicP)
{
    if (ancsBridge)
    {
        ancsBridge->characteristicDiscoveryCallback(characteristicP);
    }
}

static void bridgeDiscoveryTerminationCallback(Gap::Handle_t handle)
{
    if (ancsBridge)
    {
        ancsBridge->discoveryTerminationCallback(handle);
    }
}

static void bridgeHvxCallback(const GattHVXCallbackParams* params)
{
    if (ancsBridge)
    {
        ancsBridge->hvxCallback(params);
    }
}

static void bridgeLinkSecured(Gap::Handle_t handle, SecurityManager::SecurityMode_t mode)
{
    if (ancsBridge)
    {
        ancsBridge->linkSecured(handle, mode);
    }
}

/*****************************************************************************/

ANCSClient::ANCSClient()
    :   state(0),
        connectionHandle(0),
        expectedLength(0),
        dataLength(0)
{
    // store object
    ancsBridge = this;
}

void ANCSClient::init()
{
    BLE& ble = BLE::Instance();

    // register callbacks
    ble.gattClient().onHVX(bridgeHvxCallback);
    ble.gattServer().onDataSent(this, &ANCSClient::dataSent);
    ble.gattClient().onServiceDiscoveryTermination(bridgeDiscoveryTerminationCallback);

    // security
    ble.securityManager().init();
    ble.securityManager().onLinkSecured(bridgeLinkSecured);
}


void ANCSClient::onConnection(const Gap::ConnectionCallbackParams_t* params)
{
    DEBUGOUT("ancs: on connection\r\n");

    // store connection handle
    connectionHandle = params->handle;

    BLE& ble = BLE::Instance();

    // get current link status
    SecurityManager::LinkSecurityStatus_t securityStatus = SecurityManager::NOT_ENCRYPTED;
    ble.securityManager().getLinkSecurity(connectionHandle, &securityStatus);

    // authenticate if link is not encrypted
    if (securityStatus == SecurityManager::NOT_ENCRYPTED)
    {
        m_sec_params.bond         = SEC_PARAM_BOND;
        m_sec_params.mitm         = SEC_PARAM_MITM;
        m_sec_params.io_caps      = SEC_PARAM_IO_CAPABILITIES;
        m_sec_params.oob          = SEC_PARAM_OOB;
        m_sec_params.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
        m_sec_params.max_key_size = SEC_PARAM_MAX_KEY_SIZE;

        sd_ble_gap_authenticate(connectionHandle, &m_sec_params);
    }
    else
    {
        DEBUGOUT("link already encrypted\r\n");
    }
}

void ANCSClient::onDisconnection(const Gap::DisconnectionCallbackParams_t* params)
{
    if (params->handle == connectionHandle)
    {
        DEBUGOUT("disconnected: reset\r\n");

        connectionHandle = 0;
        state = 0;
    }
}

void ANCSClient::getNotificationAttribute(uint32_t notificationUID,
                                          notification_attribute_id_t id,
                                          uint16_t length)
{
    uint8_t payload[8];
    uint8_t payloadLength;

    payload[0] = CommandIDGetNotificationAttributes;
    payload[1] = notificationUID;
    payload[2] = notificationUID >> 8;
    payload[3] = notificationUID >> 16;
    payload[4] = notificationUID >> 24;
    payload[5] = id;

    if ((id == NotificationAttributeIDTitle) ||
        (id == NotificationAttributeIDSubtitle) ||
        (id == NotificationAttributeIDMessage))
    {
        payload[6] = length;
        payload[7] = length >> 8;

        payloadLength = 8;
    }
    else
    {
        payloadLength = 6;
    }

    BLE::Instance().gattClient().write(GattClient::GATT_OP_WRITE_REQ,
                                       connectionHandle,
                                       controlPoint.getValueHandle(),
                                       payloadLength,
                                       payload);

    dataLength = 0;
    dataPayload = SharedPointer<BlockStatic>(new BlockDynamic(length));
}


/*****************************************************************************/
/* Event handlers                                                            */
/*****************************************************************************/

void ANCSClient::hvxCallback(const GattHVXCallbackParams* params)
{
    // check that the message belongs to this connection and characteristic
    if ((params->connHandle == connectionHandle) && (params->handle == notificationSource.getValueHandle()))
    {
        Notification_t event;
        event.eventID = params->data[0];
        event.eventFlags = params->data[1];
        event.categoryID = params->data[2];
        event.categoryCount = params->data[3];

        uint32_t uid = params->data[7];
        uid = uid << 8 | params->data[6];
        uid = uid << 8 | params->data[5];
        uid = uid << 8 | params->data[4];

        event.notificationUID = uid;

        if (notificationHandler)
        {
            minar::Scheduler::postCallback(notificationHandler.bind(event));
        }
    }
    else if ((params->connHandle == connectionHandle) && (params->handle == dataSource.getValueHandle()))
    {
        // dataLength is zero when receiving first fragment
        if (dataLength == 0)
        {
            // uint8_t commandID;
            uint32_t notificationUID;
            // uint8_t attributeID;

            // commandID = params->data[0];
            notificationUID = params->data[4];
            notificationUID = notificationUID << 8 | params->data[3];
            notificationUID = notificationUID << 16 | params->data[2];
            notificationUID = notificationUID << 24 | params->data[1];
            // attributeID = params->data[5];

            dataLength = params->data[7];
            dataLength = dataLength << 8 | params->data[6];

            // copy remaining data into newly created buffer
            dataPayload->memcpy(0, &(params->data[8]), params->len - 8);

            dataOffset = params->len - 8;
        }
        else
        {
            // copy fragment into buffer and update offset
            dataPayload->memcpy(dataOffset, params->data, params->len);

            dataOffset += params->len;
        }

        // signal upper layer when all fragments have been received
        if (dataOffset >= dataLength)
        {
            dataPayload->setLength(dataLength);

            // if callback handler is set, pass sharedpointer buffer to it
            if (dataHandler)
            {
                minar::Scheduler::postCallback(dataHandler.bind(dataPayload));
            }

            // clear shared pointer; this frees the dynamic block
            dataPayload = SharedPointer<BlockStatic>();
        }
    }
}

void ANCSClient::characteristicDiscoveryCallback(const DiscoveredCharacteristic* characteristicP)
{
    DEBUGOUT("main: discovered characteristic\r\n");
    DEBUGOUT("main: uuid: %04X %02X %02X\r\n", characteristicP->getUUID().getShortUUID(),
                                               characteristicP->getValueHandle(),
                                               *((uint8_t*)&(characteristicP->getProperties())));

    uint16_t uuid = characteristicP->getUUID().getShortUUID();

    if (uuid == 0x120D)
    {
        notificationSource = *characteristicP;
        state |= FLAG_NOTIFICATION;

        DEBUGOUT("notification source: %02X\r\n", state);
    }
    else if (uuid == 0xD8F3)
    {
        controlPoint = *characteristicP;
        state |= FLAG_CONTROL;

        DEBUGOUT("control point: %02X\r\n", state);
    }
    else if (uuid == 0xC6E9)
    {
        dataSource = *characteristicP;
        state |= FLAG_DATA;

        DEBUGOUT("data source: %02X\r\n", state);
    }

    if (state == (FLAG_NOTIFICATION | FLAG_CONTROL | FLAG_DATA | FLAG_ENCRYPTION))
    {
        subscribe();
    }
}

void ANCSClient::subscribe()
{
    /* Note: Yuckiness alert! The following needs to be encapsulated in a neat API.
     * It isn't clear whether we should provide a DiscoveredCharacteristic::enableNoticiation() or
     * DiscoveredCharacteristic::discoverDescriptors() followed by DiscoveredDescriptor::write(...). */
    uint16_t value = BLE_HVX_NOTIFICATION;

    ble_error_t result = BLE_ERROR_NONE;

    if (!(state & FLAG_DATA_SUBSCRIBE))
    {
        result = BLE::Instance().gattClient().write(GattClient::GATT_OP_WRITE_CMD,
                                                    connectionHandle,
                                                    dataSource.getValueHandle() + 1, /* HACK Alert. We're assuming that CCCD descriptor immediately follows the value attribute. */
                                                    sizeof(uint16_t),                          /* HACK Alert! size should be made into a BLE_API constant. */
                                                    reinterpret_cast<const uint8_t *>(&value));

        if (result == BLE_ERROR_NONE)
        {
            state |= FLAG_DATA_SUBSCRIBE;
        }
    }

    if (!(state & FLAG_NOTIFICATION_SUBSCRIBE))
    {
        result = BLE::Instance().gattClient().write(GattClient::GATT_OP_WRITE_CMD,
                                                    connectionHandle,
                                                    notificationSource.getValueHandle() + 1, /* HACK Alert. We're assuming that CCCD descriptor immediately follows the value attribute. */
                                                    sizeof(uint16_t),                          /* HACK Alert! size should be made into a BLE_API constant. */
                                                    reinterpret_cast<const uint8_t *>(&value));

        if (result == BLE_ERROR_NONE)
        {
            state |= FLAG_NOTIFICATION_SUBSCRIBE;
        }
    }
}

void ANCSClient::dataSent(unsigned count)
{
    (void) count;

    if ((state == (FLAG_NOTIFICATION | FLAG_CONTROL | FLAG_DATA | FLAG_ENCRYPTION))
       && (!(state & FLAG_NOTIFICATION_SUBSCRIBE) || !(state & FLAG_DATA_SUBSCRIBE)))
    {
        subscribe();
    }
}

void ANCSClient::discoveryTerminationCallback(Gap::Handle_t handle)
{
    if (handle == connectionHandle)
    {
        DEBUGOUT("terminated\r\n");
    }
}

/*****************************************************************************/
/* Security                                                                  */
/*****************************************************************************/

void ANCSClient::linkSecured(Gap::Handle_t, SecurityManager::SecurityMode_t mode)
{
    state |= FLAG_ENCRYPTION;

    DEBUGOUT("Link secured: %02X\r\n", mode);

    // ancs requires a secured connection
    if (mode >= SecurityManager::SECURITY_MODE_ENCRYPTION_NO_MITM)
    {
        // find ANCS service
        const uint8_t ancs_array[] = {
            // 7905F431-B5CE-4E99-A40F-4B1E122D00D0
            0x79, 0x05, 0xF4, 0x31, 0xB5, 0xCE, 0x4E, 0x99,
            0xA4, 0x0F, 0x4B, 0x1E, 0x12, 0x2D, 0x00, 0xD0
        };

        const UUID ancs_uuid(ancs_array);

        BLE::Instance().gattClient()
                       .launchServiceDiscovery(connectionHandle,
                                               NULL,
                                               bridgeCharacteristicDiscoveryCallback,
                                               ancs_uuid);
    }
}
