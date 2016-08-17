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

// control debug output
#if 0
#include <stdio.h>
#define DEBUGOUT(...) { printf(__VA_ARGS__); }
#else
#define DEBUGOUT(...) /* nothing */
#endif // DEBUGOUT

#define MAX_DISCOVERY_RETRY 3
#define RETRY_DELAY_MS 1000

/*****************************************************************************/
/* C to C++                                                                  */
/*****************************************************************************/

static ANCSClient* ancsBridge = NULL;

static void bridgeServiceDiscoveryCallback(const DiscoveredService* service)
{
    if (ancsBridge)
    {
        ancsBridge->serviceDiscoveryCallback(service);
    }
}

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
        findService(0),
        findCharacteristics(0),
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
    ble.gap().onConnection(this, &ANCSClient::onConnection);
    ble.gap().onDisconnection(this, &ANCSClient::onDisconnection);
    ble.gattClient().onHVX(bridgeHvxCallback);
    ble.gattServer().onDataSent(this, &ANCSClient::dataSent);

    ble.gattClient()
       .onServiceDiscoveryTermination(bridgeDiscoveryTerminationCallback);

    // security
    ble.securityManager().init();
    ble.securityManager().onLinkSecured(bridgeLinkSecured);
}

void ANCSClient::getNotificationAttribute(uint32_t notificationUID,
                                          notification_attribute_id_t id,
                                          uint16_t length)
{
    uint8_t payload[8];
    uint8_t payloadLength;

    // construct notification attribute request
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

    // allocate space to store response
    dataLength = 0;
    dataPayload = SharedPointer<BlockStatic>(new BlockDynamic(length));

    // send request
    BLE::Instance().gattClient().write(GattClient::GATT_OP_WRITE_REQ,
                                       connectionHandle,
                                       controlPoint.getValueHandle(),
                                       payloadLength,
                                       payload);
}

/*****************************************************************************/
/* BLE maintainance                                                          */
/*****************************************************************************/

void ANCSClient::onConnection(const Gap::ConnectionCallbackParams_t* params)
{
    // connected as peripheral to a central
    if (params->role == Gap::PERIPHERAL)
    {
        connectionHandle = params->handle;

        minar::Scheduler::postCallback(this, &ANCSClient::startServiceDiscovery);
    }
}

void ANCSClient::startServiceDiscovery()
{
    DEBUGOUT("ancs: service discovery begin\r\n");

    BLE& ble = BLE::Instance();

    if (ble.gattClient().isServiceDiscoveryActive() == false)
    {
        ble.gattClient()
           .launchServiceDiscovery(connectionHandle,
                                   bridgeServiceDiscoveryCallback,
                                   NULL,
                                   ANCS::UUID);
    }
    else
    {
        findService = MAX_DISCOVERY_RETRY;
    }
}

void ANCSClient::serviceDiscoveryCallback(const DiscoveredService*)
{
    DEBUGOUT("ancs: found service\r\n");

    // terminate discovery
    findService = 0;
    BLE::Instance().gattClient().terminateServiceDiscovery();

    // secure connection so we can access characteristics
    minar::Scheduler::postCallback(this, &ANCSClient::secureConnection);
}

void ANCSClient::secureConnection()
{
    BLE& ble = BLE::Instance();

    // get current link status
    SecurityManager::LinkSecurityStatus_t securityStatus = SecurityManager::NOT_ENCRYPTED;
    ble.securityManager().getLinkSecurity(connectionHandle, &securityStatus);

    // do discovery when connection is encrypted
    findCharacteristics = MAX_DISCOVERY_RETRY;

    // authenticate if link is not encrypted
    if (securityStatus == SecurityManager::NOT_ENCRYPTED)
    {
        ble.securityManager().setLinkSecurity(connectionHandle, SecurityManager::SECURITY_MODE_ENCRYPTION_NO_MITM);
    }
    else
    {
        DEBUGOUT("ancs: link already encrypted\r\n");

        minar::Scheduler::postCallback(this, &ANCSClient::startCharacteristicDiscovery);
    }
}

void ANCSClient::linkSecured(Gap::Handle_t, SecurityManager::SecurityMode_t mode)
{
    (void) mode;

    state |= FLAG_ENCRYPTION;

    DEBUGOUT("ancs: link secured: %02X\r\n", mode);

    if (findCharacteristics)
    {
        minar::Scheduler::postCallback(this, &ANCSClient::startCharacteristicDiscovery);
    }
}

void ANCSClient::startCharacteristicDiscovery()
{
    DEBUGOUT("ancs: characteristic discovery begin\r\n");

    BLE& ble = BLE::Instance();

    if (ble.gattClient().isServiceDiscoveryActive() == false)
    {
        ble.gattClient()
           .launchServiceDiscovery(connectionHandle,
                                   NULL,
                                   bridgeCharacteristicDiscoveryCallback,
                                   ANCS::UUID);
    }
    else
    {
        findCharacteristics = MAX_DISCOVERY_RETRY;
    }
}

void ANCSClient::characteristicDiscoveryCallback(const DiscoveredCharacteristic* characteristicP)
{
    DEBUGOUT("ancs: discovered characteristic\r\n");
    DEBUGOUT("ancs: uuid: %04X %02X %02X\r\n", characteristicP->getUUID().getShortUUID(),
                                               characteristicP->getValueHandle(),
                                               *((uint8_t*)&(characteristicP->getProperties())));

    uint16_t uuid = characteristicP->getUUID().getShortUUID();

    if (uuid == 0x120D)
    {
        notificationSource = *characteristicP;
        state |= FLAG_NOTIFICATION;

        DEBUGOUT("ancs: notification source: %02X\r\n", state);
    }
    else if (uuid == 0xD8F3)
    {
        controlPoint = *characteristicP;
        state |= FLAG_CONTROL;

        DEBUGOUT("ancs: control point: %02X\r\n", state);
    }
    else if (uuid == 0xC6E9)
    {
        dataSource = *characteristicP;
        state |= FLAG_DATA;

        DEBUGOUT("ancs: data source: %02X\r\n", state);
    }

    if (state == (FLAG_NOTIFICATION | FLAG_CONTROL | FLAG_DATA | FLAG_ENCRYPTION))
    {
        DEBUGOUT("ancs: subscribe\r\n");

        findCharacteristics = 0;
        BLE::Instance().gattClient().terminateServiceDiscovery();

        minar::Scheduler::postCallback(this, &ANCSClient::subscribe);

        // signal service found
        if (serviceFoundHandler)
        {
            minar::Scheduler::postCallback(serviceFoundHandler);
        }
    }
}

void ANCSClient::subscribe()
{
    /* Note: Yuckiness alert! The following needs to be encapsulated in a neat API.
     * It isn't clear whether we should provide a DiscoveredCharacteristic::enableNoticiation() or
     * DiscoveredCharacteristic::discoverDescriptors() followed by DiscoveredDescriptor::write(...). */
    const uint16_t value = BLE_HVX_NOTIFICATION;

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
            DEBUGOUT("ancs: data subscribe sent\r\n");

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
            DEBUGOUT("ancs: notification subscribe sent\r\n");

            state |= FLAG_NOTIFICATION_SUBSCRIBE;
        }
    }
}

void ANCSClient::discoveryTerminationCallback(Gap::Handle_t handle)
{
    if (handle == connectionHandle)
    {
        if (findService)
        {
            // decrement retry counter and post callback
            findService--;

            minar::Scheduler::postCallback(this, &ANCSClient::startServiceDiscovery)
                .delay(minar::milliseconds(RETRY_DELAY_MS));
        }
        else if (findCharacteristics)
        {
            // decrement retry counter and post callback
            findCharacteristics--;

            minar::Scheduler::postCallback(this, &ANCSClient::startCharacteristicDiscovery)
                .delay(minar::milliseconds(RETRY_DELAY_MS));
        }
        else
        {
            DEBUGOUT("ancs: discovery done\r\n");
        }
    }
}

void ANCSClient::onDisconnection(const Gap::DisconnectionCallbackParams_t* params)
{
    if (params->handle == connectionHandle)
    {
        DEBUGOUT("ancs: disconnected: reset\r\n");

        connectionHandle = 0;
        findService = 0;
        findCharacteristics = 0;
        state = 0;
    }
}

/*****************************************************************************/
/* Event handlers                                                            */
/*****************************************************************************/

void ANCSClient::hvxCallback(const GattHVXCallbackParams* params)
{
    // check that the message belongs to this connection and characteristic
    if ((params->connHandle == connectionHandle) &&
        (params->handle == notificationSource.getValueHandle()) &&
        (notificationHandler))
    {
        // parse data to notification event
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

        // only process newly added notifications that are not silent
        if ((event.eventID == ANCSClient::EventIDNotificationAdded) &&
            !(event.eventFlags & ANCSClient::EventFlagSilent))
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

void ANCSClient::dataSent(unsigned count)
{
    (void) count;

    if ((state == (FLAG_NOTIFICATION | FLAG_CONTROL | FLAG_DATA | FLAG_ENCRYPTION))
       && (!(state & FLAG_NOTIFICATION_SUBSCRIBE) || !(state & FLAG_DATA_SUBSCRIBE)))
    {
        subscribe();
    }
}

