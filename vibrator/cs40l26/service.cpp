/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>
#include <log/log.h>

#include "Hardware.h"
#include "Vibrator.h"
#include "VibratorSync.h"

using ::aidl::android::hardware::vibrator::HwApi;
using ::aidl::android::hardware::vibrator::HwCal;
using ::aidl::android::hardware::vibrator::Vibrator;
using ::android::defaultServiceManager;
using ::android::ProcessState;
using ::android::sp;
using ::android::String16;
using ::android::hardware::vibrator::VibratorSync;

int main() {
    const char *inputEventName = std::getenv("INPUT_EVENT_NAME");
    std::string vibName = "";
    if (strstr(inputEventName, "cs40l26_input") != nullptr) {
        vibName.assign("default");
    } else if (strstr(inputEventName, "cs40l26_dual_input") != nullptr) {
        vibName.assign("dual");
    } else {
        ALOGE("Failed to init vibrator HAL");
        return EXIT_FAILURE;  // should not reach
    }
    auto svc = ndk::SharedRefBase::make<Vibrator>(std::make_unique<HwApi>(),
                                                  std::make_unique<HwCal>());
    const auto svcName = std::string() + svc->descriptor + "/" + vibName;

    auto ext = sp<VibratorSync>::make(svc);
    const auto extName = std::stringstream() << ext->descriptor << "/" << vibName;

    ProcessState::initWithDriver("/dev/vndbinder");

    defaultServiceManager()->addService(String16(extName.str().c_str()), ext);

    auto svcBinder = svc->asBinder();
    binder_status_t status = AServiceManager_addService(svcBinder.get(), svcName.c_str());
    LOG_ALWAYS_FATAL_IF(status != STATUS_OK);

    ProcessState::self()->setThreadPoolMaxThreadCount(1);
    ProcessState::self()->startThreadPool();

    ABinderProcess_setThreadPoolMaxThreadCount(0);
    ABinderProcess_joinThreadPool();

    return EXIT_FAILURE;  // should not reach
}
