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

#include "mbed.h"

#include "BLE/ble.h"

#include "ANCSClient.h"

#include <string>

const uint8_t ancs_array[] = { 0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4,
                               0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79 };
const UUID uuid(ancs_array);

const char DEVICE_NAME[] = "ANCS";

#ifndef CFG_BLE_TX_POWER_LEVEL
#define CFG_BLE_TX_POWER_LEVEL 0
#endif

static BLE ble;

// debug led - blinks to show liveness
static Ticker ticker;
static DigitalOut mbed_led1(LED1);

// enable buttons to initiate transfer
static InterruptIn button1(BUTTON1);
static InterruptIn button2(BUTTON2);

static ANCSClient ancs;

/*****************************************************************************/
/* Debug                                                                     */
/*****************************************************************************/
/*
    Called once every second. Blinks LED.
*/
void periodicCallback(void)
{
    mbed_led1 = !mbed_led1;
}

/*****************************************************************************/
/* Buttons                                                                   */
/*****************************************************************************/

/*
*/
void button1ISR()
{
    printf("Button: 1\r\n");
}

void button2ISR()
{
    printf("Button: 2\r\n");
}

/*****************************************************************************/
/* ANCS                                                                      */
/*****************************************************************************/

uint8_t attributeIndex = 0;
uint32_t notificationID = 0;

void onNotification(ANCSClient::Notification_t event)
{
    // only process newly added notifications that are not silent
    if ((event.eventID == ANCSClient::EventIDNotificationAdded) &&
        !(event.eventFlags & ANCSClient::EventFlagSilent))
    {
        printf("main: %u %u %u %u %lu\r\n", event.eventID, event.eventFlags, event.categoryID, event.categoryCount, event.notificationUID);

        notificationID = event.notificationUID;

//    ancs.getNotificationAttribute(event.notificationUID, ANCSClient::NotificationAttributeIDTitle, 100);
//        ancs.getNotificationAttribute(event.notificationUID, ANCSClient::NotificationAttributeIDMessage, 100);
        ancs.getNotificationAttribute(notificationID, (ANCSClient::notification_attribute_id_t) attributeIndex++, 100);
    }
}

void onData(SharedPointer<Block> dataPayload)
{
    printf("data: ");
    for (uint8_t idx = 0; idx < dataPayload->getLength(); idx++)
    {
        printf("%c", dataPayload->at(idx));
    }
    printf("\r\n");

    if (attributeIndex < 8)
    {
        ancs.getNotificationAttribute(notificationID, (ANCSClient::notification_attribute_id_t) attributeIndex++, 100);
    }
    else
    {
        attributeIndex = 0;
        printf("\r\n");
    }

}

/*****************************************************************************/
/* BLE                                                                       */
/*****************************************************************************/


void whenConnected(const Gap::ConnectionCallbackParams_t* params)
{
    printf("ble: Connected: %d %d %d %d\r\n", params->connectionParams->minConnectionInterval,
                                                      params->connectionParams->maxConnectionInterval,
                                                      params->connectionParams->slaveLatency,
                                                      params->connectionParams->connectionSupervisionTimeout);

    ancs.init(params->handle);
    ancs.registerNotificationHandler(onNotification);
    ancs.registerDataHandler(onData);
}

void whenDisconnected(const Gap::DisconnectionCallbackParams_t* params)
{
    (void) params;

    printf("ble: Disconnected!\r\n");
    printf("ble: Restarting the advertising process\r\n");

    ble.gap().startAdvertising();
}

/*****************************************************************************/
/* App start                                                                 */
/*****************************************************************************/
void app_start(int, char *[])
{
    // setup buttons
    button1.mode(PullUp);
    // Delay for initial pullup to take effect
    wait(.01);
    button1.fall(button1ISR);

    button2.mode(PullUp);
    // Delay for initial pullup to take effect
    wait(.01);
    button2.fall(button2ISR);

    // blink led
    ticker.attach_us(periodicCallback, 1000 * 1000);

    /*************************************************************************/
    /*************************************************************************/
    ble.init();

    // Apple uses device name instead of beacon name
    ble.gap().setDeviceName((const uint8_t*) DEVICE_NAME);

    // set TX power
    ble.gap().setTxPower(CFG_BLE_TX_POWER_LEVEL);

    // connection status handlers
    ble.gap().onConnection(whenConnected);
    ble.gap().onDisconnection(whenDisconnected);

    // construct advertising beacon
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::BREDR_NOT_SUPPORTED|GapAdvertisingData::LE_GENERAL_DISCOVERABLE);
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::SHORTENED_LOCAL_NAME, (const uint8_t *) DEVICE_NAME, sizeof(DEVICE_NAME) - 1);
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::LIST_128BIT_SOLICITATION_IDS, uuid.getBaseUUID(), uuid.getLen());
    ble.gap().accumulateAdvertisingPayloadTxPower(CFG_BLE_TX_POWER_LEVEL);

    ble.gap().setAdvertisingType(GapAdvertisingParams::ADV_CONNECTABLE_UNDIRECTED);
    ble.gap().setAdvertisingInterval(152);

    ble.gap().startAdvertising();

    printf("ANCS Client: %s %s\r\n", __DATE__, __TIME__);
}

/*****************************************************************************/
/* Compatibility                                                             */
/*****************************************************************************/

#if defined(YOTTA_MINAR_VERSION_STRING)
/*********************************************************/
/* Build for mbed OS                                     */
/*********************************************************/

#else
/*********************************************************/
/* Build for mbed Classic                                */
/*********************************************************/
int main(void)
{
    app_start(0, NULL);

    for(;;)
    {
        BLEProxy::callFromMainLoop();
        ble.waitForEvent();
    }
}
#endif
