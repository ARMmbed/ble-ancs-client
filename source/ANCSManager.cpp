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

#include "ble-ancs-client/ANCSManager.h"

#include "ble-ancs-client/ANCSClient.h"
#include "cborg/Cbore.h"

#include <string>

// control debug output
#if 1
#include <stdio.h>
#define DEBUGOUT(...) { printf(__VA_ARGS__); }
#else
#define DEBUGOUT(...) /* nothing */
#endif // DEBUGOUT

#define ALERT_LEVEL 1
#define MAX_RETRIEVE_LENGTH 110

static ANCSClient ancs;

static FunctionPointer1<void, SharedPointer<BlockStatic> > receiveHandler;

static SharedPointer<BlockStatic> sendBlock;
static SharedPointer<BlockStatic> titleBlock;

static ANCSClient::notification_attribute_id_t attributeIndex;
static uint32_t notificationID = 0;

static void onNotificationTask(ANCSClient::Notification_t event);
static void onNotificationAttributeTask(SharedPointer<BlockStatic> dataPayload);

/*****************************************************************************/
/* ANCS                                                                      */
/*****************************************************************************/

void ANCSManager::init()
{
    ancs.init();

    ancs.registerNotificationHandlerTask(onNotificationTask);
    ancs.registerDataHandlerTask(onNotificationAttributeTask);
}

void ANCSManager::onConnection(Gap::Handle_t handle)
{
    ancs.onConnection(handle);
}

void ANCSManager::onReceive(FunctionPointer1<void, SharedPointer<BlockStatic> > callback)
{
    receiveHandler = callback;
}

static void onNotificationTask(ANCSClient::Notification_t event)
{
    // only process newly added notifications that are not silent
    if ((event.eventID == ANCSClient::EventIDNotificationAdded) &&
        !(event.eventFlags & ANCSClient::EventFlagSilent))
    {
        DEBUGOUT("main: %u %u %u %u %lu\r\n", event.eventID, event.eventFlags, event.categoryID, event.categoryCount, event.notificationUID);

        notificationID = event.notificationUID;

        attributeIndex = ANCSClient::NotificationAttributeIDTitle;
        ancs.getNotificationAttribute(notificationID, attributeIndex, MAX_RETRIEVE_LENGTH);
    }
}

static void onNotificationAttributeTask(SharedPointer<BlockStatic> dataPayload)
{
    DEBUGOUT("data: ");
    for (uint8_t idx = 0; idx < dataPayload->getLength(); idx++)
    {
        DEBUGOUT("%c", dataPayload->at(idx));
    }
    DEBUGOUT("\r\n");

    if (attributeIndex == ANCSClient::NotificationAttributeIDTitle)
    {
        // store title payload
        titleBlock = dataPayload;

        // get message
        attributeIndex = ANCSClient::NotificationAttributeIDMessage;
        ancs.getNotificationAttribute(notificationID, attributeIndex, MAX_RETRIEVE_LENGTH);
    }
    else if (attributeIndex == ANCSClient::NotificationAttributeIDMessage)
    {
        // allocate buffer
        uint8_t cborLength = 1 + 1                          // array and alert level
                           + 2 + titleBlock->getLength()    // title
                           + 2 + dataPayload->getLength();  // message

        sendBlock = SharedPointer<BlockStatic>(new BlockDynamic(cborLength));

        // construct cbor
        Cbore cbor(sendBlock->getData(), sendBlock->getLength());

        cbor.array(3)
            .item(ALERT_LEVEL)
            .item((const char*) titleBlock->getData(), titleBlock->getLength())
            .item((const char*) dataPayload->getData(), dataPayload->getLength());

        // set length
        sendBlock->setLength(cbor.getLength());

        // pass block to callback handler, if set
        if (receiveHandler)
        {
            minar::Scheduler::postCallback(receiveHandler.bind(sendBlock));
        }

        // flag buffers as done
        titleBlock = SharedPointer<BlockStatic>();
        sendBlock = SharedPointer<BlockStatic>();
    }
}
