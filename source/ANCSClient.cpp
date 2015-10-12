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

#include "ANCSClient.h"

#include <stdio.h>

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

ANCSClient* ancsBridge = NULL;

void bridgeCharacteristicDiscoveryCallback(const DiscoveredCharacteristic* characteristicP)
{
    if (ancsBridge)
    {
        ancsBridge->characteristicDiscoveryCallback(characteristicP);
    }
}
/*****************************************************************************/

ANCSClient::ANCSClient()
    :   state(0),
        connectionHandle(0),
        dataLength(0)
{}

void ANCSClient::init(Gap::Handle_t connection)
{
    // store connection handle
    connectionHandle = connection;

    // setup ble callbacks
    BLE ble;
    ble.init();

    ble.gap().onDisconnection(this, &ANCSClient::whenDisconnected);

    ble.gattClient().onHVX(this, &ANCSClient::hvxCallback);
    ble.gattServer().onDataSent(this, &ANCSClient::dataSent);

    // security
    ble.securityManager().init();
    ble.securityManager().onSecuritySetupInitiated(this, &ANCSClient::securityInitiated);
    ble.securityManager().onSecuritySetupCompleted(this, &ANCSClient::securityCompleted);
    ble.securityManager().onLinkSecured(this, &ANCSClient::linkSecured);
    ble.securityManager().onSecurityContextStored(this, &ANCSClient::contextStored);
    ble.securityManager().onPasskeyDisplay(this, &ANCSClient::passkeyDisplay);

    // store object
    ancsBridge = this;

    // find ANCS service
    const uint8_t ancs_array[] = {
        // 7905F431-B5CE-4E99-A40F-4B1E122D00D0
        0x79, 0x05, 0xF4, 0x31, 0xB5, 0xCE, 0x4E, 0x99,
        0xA4, 0x0F, 0x4B, 0x1E, 0x12, 0x2D, 0x00, 0xD0
    };

    const UUID ancs_uuid(ancs_array);

    ble.gattClient().launchServiceDiscovery(connectionHandle,
                                            NULL,
                                            bridgeCharacteristicDiscoveryCallback,
                                            ancs_uuid);

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
        printf("link already encrypted\r\n");
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

    BLE ble;
    ble.gattClient().write(GattClient::GATT_OP_WRITE_REQ,
                           connectionHandle,
                           controlPoint.getValueHandle(),
                           payloadLength,
                           payload);
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
            notificationHandler.call(event);
        }
    }
    else if ((params->connHandle == connectionHandle) && (params->handle == dataSource.getValueHandle()))
    {
        // dataLength is zero when receiving first fragment
        if (dataLength == 0)
        {
            uint8_t commandID;
            uint32_t notificationUID;
            uint8_t attributeID;

            commandID = params->data[0];
            notificationUID = params->data[4];
            notificationUID = notificationUID << 8 | params->data[3];
            notificationUID = notificationUID << 16 | params->data[2];
            notificationUID = notificationUID << 24 | params->data[1];
            attributeID = params->data[5];

            dataLength = params->data[7];
            dataLength = dataLength << 8 | params->data[6];

            dataPayload = SharedPointer<Block>(new BlockDynamic(dataLength));

            // copy remaining data into newly created buffer
            dataPayload->memcpy(dataOffset, &(params->data[8]), params->len - 8);

            dataOffset = params->len - 8;
        }
        else
        {
            // copy fragment into buffer and update offset
            dataPayload->memcpy(dataOffset, params->data, params->len);

            dataOffset += params->len;
        }

        // signal upper layer when all fragments have been received
        if (dataLength == dataOffset)
        {
            // reset book keeping variables
            dataLength = 0;
            dataOffset = 0;

            // if callback handler is set, pass sharedpointer buffer to it
            if (dataHandler)
            {
                dataHandler.call(dataPayload);
            }

            // clear shared pointer; this frees the dynamic block
            dataPayload = SharedPointer<Block>();
        }
    }
}

void ANCSClient::whenDisconnected(const Gap::DisconnectionCallbackParams_t* params)
{
    if (params->handle == connectionHandle)
    {
        printf("disconnected: reset\r\n");

        connectionHandle = 0;
        state = 0;
    }
}

void ANCSClient::characteristicDiscoveryCallback(const DiscoveredCharacteristic* characteristicP)
{
    printf("main: discovered characteristic\r\n");
    printf("main: uuid: %04X %02X %02X\r\n", characteristicP->getUUID().getShortUUID(),
                                               characteristicP->getValueHandle(),
                                               *((uint8_t*)&(characteristicP->getProperties())));

    uint16_t uuid = characteristicP->getUUID().getShortUUID();

    if (uuid == 0x120D)
    {
        notificationSource = *characteristicP;
        state |= FLAG_NOTIFICATION;

        printf("notification source: %02X\r\n", state);
    }
    else if (uuid == 0xD8F3)
    {
        controlPoint = *characteristicP;
        state |= FLAG_CONTROL;

        printf("control point: %02X\r\n", state);
    }
    else if (uuid == 0xC6E9)
    {
        dataSource = *characteristicP;
        state |= FLAG_DATA;

        printf("data source: %02X\r\n", state);
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

    BLE ble;
    ble_error_t result = BLE_ERROR_NONE;

    if (!(state & FLAG_DATA_SUBSCRIBE))
    {
        result = ble.gattClient().write(GattClient::GATT_OP_WRITE_CMD,
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
        result = ble.gattClient().write(GattClient::GATT_OP_WRITE_CMD,
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

/*****************************************************************************/
/* Security                                                                  */
/*****************************************************************************/

void ANCSClient::securityInitiated(Gap::Handle_t, bool, bool, SecurityManager::SecurityIOCapabilities_t)
{
    printf("Security started\r\n");
}

void ANCSClient::securityCompleted(Gap::Handle_t, SecurityManager::SecurityCompletionStatus_t)
{
    printf("Security completed\r\n");
}

void ANCSClient::linkSecured(Gap::Handle_t, SecurityManager::SecurityMode_t)
{
    state |= FLAG_ENCRYPTION;

    printf("Link secured: %02X\r\n", state);

    if (state == (FLAG_NOTIFICATION | FLAG_CONTROL | FLAG_DATA | FLAG_ENCRYPTION))
    {
        subscribe();
    }
}

void ANCSClient::contextStored(Gap::Handle_t)
{
    printf("Context stored\r\n");
}

void ANCSClient::passkeyDisplay(Gap::Handle_t, const SecurityManager::Passkey_t)
{
    printf("Display passkey\r\n");
}
