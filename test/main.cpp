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


#include "mbed-drivers/mbed.h"
#include "ble/BLE.h"

#include "ble-ancs-client/ANCSClient.h"

#include <string>
#include <queue>

/*****************************************************************************/
/* Configuration                                                             */
/*****************************************************************************/

// set default device name
#define DEVICE_NAME "ANCS";

// set TX power
#ifndef CFG_BLE_TX_POWER_LEVEL
#define CFG_BLE_TX_POWER_LEVEL 0
#endif

// control debug output
#if !defined(TARGET_LIKE_WATCH)
#define DEBUGOUT(...) { printf(__VA_ARGS__); }
#else
#define DEBUGOUT(...) /* nothing */
#endif // DEBUGOUT

#define VERBOSE_DEBUG_OUT 0

#define MAX_RETRIEVE_LENGTH 110

/*****************************************************************************/
/* Variables used by the app                                                 */
/*****************************************************************************/

const uint8_t txPowerLevel = CFG_BLE_TX_POWER_LEVEL;

std::string deviceNameString;

BLE ble;
Gap::Handle_t connectionHandle;

ANCSClient ancs;
ANCSClient::notification_attribute_id_t attributeIndex;
uint32_t notificationID = 0;

std::queue<uint32_t> notificationQueue;

/*****************************************************************************/
/* Debug                                                                     */
/*****************************************************************************/

// debug led - blinks to show liveness
Ticker ticker;
DigitalOut mbed_led1(LED1);

void periodicCallbackISR(void)
{
    mbed_led1 = !mbed_led1;
}

/*****************************************************************************/
/* ANCS                                                                      */
/*****************************************************************************/

void processQueue()
{
    DEBUGOUT("ancs: process queue: %d\r\n", notificationQueue.size());

    notificationID = notificationQueue.front();

    attributeIndex = ANCSClient::NotificationAttributeIDTitle;
    ancs.getNotificationAttribute(notificationID, attributeIndex, MAX_RETRIEVE_LENGTH);
}

void onNotificationTask(ANCSClient::Notification_t event)
{
    // only process newly added notifications that are not silent
    if ((event.eventID == ANCSClient::EventIDNotificationAdded) &&
        !(event.eventFlags & ANCSClient::EventFlagSilent))
    {
        DEBUGOUT("ancs: %u %u %u %u %lu\r\n", event.eventID, event.eventFlags, event.categoryID, event.categoryCount, event.notificationUID);

        notificationQueue.push(event.notificationUID);

        if (notificationQueue.size() == 1)
        {
            minar::Scheduler::postCallback(processQueue);
        }
    }
}

void onNotificationAttributeTask(SharedPointer<BlockStatic> dataPayload)
{
    DEBUGOUT("data: ");
    for (uint8_t idx = 0; idx < dataPayload->getLength(); idx++)
    {
        DEBUGOUT("%c", dataPayload->at(idx));
    }
    DEBUGOUT("\r\n");

    if (attributeIndex == ANCSClient::NotificationAttributeIDTitle)
    {
        // get subtitle
        attributeIndex = ANCSClient::NotificationAttributeIDSubtitle;
        ancs.getNotificationAttribute(notificationID, attributeIndex, MAX_RETRIEVE_LENGTH);
    }
    else if (attributeIndex == ANCSClient::NotificationAttributeIDSubtitle)
    {
        // get message
        attributeIndex = ANCSClient::NotificationAttributeIDMessage;
        ancs.getNotificationAttribute(notificationID, attributeIndex, MAX_RETRIEVE_LENGTH);
    }
    else if (attributeIndex == ANCSClient::NotificationAttributeIDMessage)
    {
        // remove ID from queue
        notificationQueue.pop();

        // process next ID if available
        if (notificationQueue.size() > 0)
        {
            minar::Scheduler::postCallback(processQueue);
        }
    }
}

/*****************************************************************************/
/* BLE                                                                       */
/*****************************************************************************/

/*
    Functions called when BLE device connects and disconnects.
*/
void onConnection(const Gap::ConnectionCallbackParams_t* params)
{
    DEBUGOUT("main: Connected: %d %d %d\r\n", params->connectionParams->minConnectionInterval,
                                              params->connectionParams->maxConnectionInterval,
                                              params->connectionParams->slaveLatency);

    DEBUGOUT("main: %02X %02X %02X %02X %02X %02X %02X\r\n", params->peerAddrType,
        params->peerAddr[0],
        params->peerAddr[1],
        params->peerAddr[2],
        params->peerAddr[3],
        params->peerAddr[4],
        params->peerAddr[5]);
}

void onDisconnection(const Gap::DisconnectionCallbackParams_t* params)
{
    DEBUGOUT("main: Disconnected!\r\n");
    DEBUGOUT("main: Restarting the advertising process\r\n");

    ble.gap().startAdvertising();
}

void updateAdvertisement()
{
    ble.gap().stopAdvertising();

    /*************************************************************************/

    ble.gap().clearAdvertisingPayload();
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::BREDR_NOT_SUPPORTED|GapAdvertisingData::LE_GENERAL_DISCOVERABLE);
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::LIST_128BIT_SOLICITATION_IDS, ANCS::UUID.getBaseUUID(), ANCS::UUID.getLen());

    /*************************************************************************/

    ble.gap().clearScanResponse();
    ble.gap().accumulateScanResponse(GapAdvertisingData::TX_POWER_LEVEL, &txPowerLevel, 1);
    ble.gap().accumulateScanResponse(GapAdvertisingData::COMPLETE_LOCAL_NAME, (uint8_t*) deviceNameString.c_str(), deviceNameString.length());

    /*************************************************************************/

    ble.gap().setAdvertisingType(GapAdvertisingParams::ADV_CONNECTABLE_UNDIRECTED);
    ble.gap().setAdvertisingInterval(319);
    ble.gap().startAdvertising();
}

void bleInitDone(BLE::InitializationCompleteCallbackContext* context)
{
    (void) context;

    deviceNameString = DEVICE_NAME;

    // status callback functions
    ble.gap().onConnection(onConnection);
    ble.gap().onDisconnection(onDisconnection);

    ble.gap().setAddress(Gap::ADDR_TYPE_RANDOM_STATIC, NULL);
    ble.gap().setDeviceName((const uint8_t*) deviceNameString.c_str());
    ble.gap().setTxPower(txPowerLevel);

    updateAdvertisement();

    /*************************************************************************/

    ancs.init();
    ancs.registerNotificationHandlerTask(onNotificationTask);
    ancs.registerDataHandlerTask(onNotificationAttributeTask);

    DEBUGOUT("ANCS Client: %s %s\r\n", __DATE__, __TIME__);
}

/*****************************************************************************/
/* main                                                                      */
/*****************************************************************************/

void app_start(int, char *[])
{
    // blink led
    ticker.attach(periodicCallbackISR, 1.0);

    /*************************************************************************/

    /* bluetooth le */
    ble.init(bleInitDone);
}
