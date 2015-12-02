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

#include "ble-ancs-client/ANCSManager.h"

#include <string>

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

const uint8_t txPowerLevel = CFG_BLE_TX_POWER_LEVEL;

/*
const uint8_t ancsArray[] = { 0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4,
                              0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79 };
*/

const UUID ancsUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0");

std::string deviceNameString;

/*****************************************************************************/
/* Variables used by the app                                                 */
/*****************************************************************************/

static BLE ble;

static Gap::Handle_t connectionHandle;

/*****************************************************************************/
/* Debug                                                                     */
/*****************************************************************************/

// debug led - blinks to show liveness
static Ticker ticker;
static DigitalOut mbed_led1(LED1);

// enable buttons to initiate transfer
static InterruptIn button1(BUTTON1);
static InterruptIn button2(BUTTON2);

void periodicCallbackISR(void)
{
    mbed_led1 = !mbed_led1;
}

void button1Task()
{

}

void button2Task()
{

}

void button1ISR()
{
    minar::Scheduler::postCallback(button1Task);
}

void button2ISR()
{
    minar::Scheduler::postCallback(button2Task);
}

/*****************************************************************************/
/* BLE                                                                       */
/*****************************************************************************/

void bridgeServiceDiscoveryCallback(const DiscoveredService*)
{
    DEBUGOUT("main: found service\r\n");

    ANCSManager::onConnection(connectionHandle);
}

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

    // connected as peripheral to a central
    if (params->role == Gap::PERIPHERAL)
    {
        connectionHandle = params->handle;

        BLE::Instance().gattClient()
                       .launchServiceDiscovery(connectionHandle,
                                               bridgeServiceDiscoveryCallback,
                                               NULL,
                                               ancsUUID);
    }
}

void onDisconnection(const Gap::DisconnectionCallbackParams_t* params)
{
    DEBUGOUT("main: Disconnected!\r\n");

    // disconnected from central
    if (params->handle == connectionHandle)
    {
        connectionHandle = 0;
    }

    DEBUGOUT("main: Restarting the advertising process\r\n");

    ble.gap().startAdvertising();
}

/*****************************************************************************/
/* main                                                                      */
/*****************************************************************************/

void received(SharedPointer<BlockStatic> block)
{
    for (size_t idx = 0; idx < block->getLength(); idx++)
    {
        DEBUGOUT("%02X", block->at(idx));
    }
    DEBUGOUT("\r\n");
}

void updateAdvertisement()
{
    ble.gap().stopAdvertising();

    /*************************************************************************/

    ble.gap().clearAdvertisingPayload();
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::BREDR_NOT_SUPPORTED|GapAdvertisingData::LE_GENERAL_DISCOVERABLE);
    ble.gap().accumulateAdvertisingPayload(GapAdvertisingData::LIST_128BIT_SOLICITATION_IDS, ancsUUID.getBaseUUID(), ancsUUID.getLen());

    /*************************************************************************/

    ble.gap().clearScanResponse();
    ble.gap().accumulateScanResponse(GapAdvertisingData::TX_POWER_LEVEL, &txPowerLevel, 1);
    ble.gap().accumulateScanResponse(GapAdvertisingData::COMPLETE_LOCAL_NAME, (uint8_t*) deviceNameString.c_str(), deviceNameString.length());

    /*************************************************************************/

    ble.gap().setAdvertisingType(GapAdvertisingParams::ADV_CONNECTABLE_UNDIRECTED);
    ble.gap().setAdvertisingInterval(319);
    ble.gap().startAdvertising();
}

static void bleInitDone(BLE::InitializationCompleteCallbackContext* context)
{
    (void) context;

    ANCSManager::init();
    ANCSManager::onReceive(received);

    deviceNameString = DEVICE_NAME;

    // status callback functions
    ble.gap().onConnection(onConnection);
    ble.gap().onDisconnection(onDisconnection);

    ble.gap().setAddress(Gap::ADDR_TYPE_RANDOM_STATIC, NULL);
    ble.gap().setDeviceName((const uint8_t*) deviceNameString.c_str());
    ble.gap().setTxPower(txPowerLevel);

    updateAdvertisement();

    /*************************************************************************/

    DEBUGOUT("ANCS Client: %s %s\r\n", __DATE__, __TIME__);

    const uint8_t* buffer = ancsUUID.getBaseUUID();

    for (size_t idx = 0; idx < 16; idx++)
    {
        DEBUGOUT("%02X", buffer[idx]);
    }
    DEBUGOUT("\r\n");
}

void app_start(int, char *[])
{
    // setup buttons
    button1.fall(button1ISR);
    button2.fall(button2ISR);

    // blink led
    ticker.attach(periodicCallbackISR, 1.0);

    /*************************************************************************/

    /* bluetooth le */
    ble.init(bleInitDone);
}
