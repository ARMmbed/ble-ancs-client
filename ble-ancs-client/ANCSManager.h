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

#ifndef __ANCS_MANAGER_H__
#define __ANCS_MANAGER_H__

#include "mbed-drivers/mbed.h"

#include "ble-ancs-client/ANCSClient.h"
#include "cborg/Cbor.h"

#include "core-util/SharedPointer.h"
#include "mbed-block/BlockStatic.h"

using namespace mbed::util;

namespace ANCSManager
{
    void init();

    void onReceive(FunctionPointer1<void, SharedPointer<BlockStatic> > callback);

    template <typename T>
    void onReceive(T* object, void (T::*member)(SharedPointer<BlockStatic>))
    {
        FunctionPointer1<void, SharedPointer<BlockStatic> > fp(object, member);

        onReceive(fp);
    }
}

#endif // __ANCS_MANAGER_H__
