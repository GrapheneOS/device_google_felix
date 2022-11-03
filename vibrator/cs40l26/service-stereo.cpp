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

#include "VibMgrHwApi.h"
#include "VibratorManager.h"

using ::aidl::android::hardware::vibrator::IVibrator;
using ::aidl::android::hardware::vibrator::VibMgrHwApi;
using ::aidl::android::hardware::vibrator::VibratorManager;
using ::android::ProcessState;
using ::android::String16;
using ::android::waitForService;
using ::android::hardware::vibrator::IVibratorSync;

#if !defined(VIBRATOR_NAME)
#define VIBRATOR_NAME "default"
#endif

using ndk::SharedRefBase;
using ndk::SpAIBinder;

int main() {
    auto hwapi = VibMgrHwApi::Create();
    if (!hwapi) {
        return EXIT_FAILURE;
    }

    const std::string vibratorInstances[] = {
            "default",
            "dual",
    };
    std::vector<VibratorManager::VibratorTuple> vibrators;

    ProcessState::initWithDriver("/dev/vndbinder");

    for (auto &instance : vibratorInstances) {
        const auto svcName = std::string() + IVibrator::descriptor + "/" + instance;
        const auto extName = std::stringstream() << IVibratorSync::descriptor << "/" << instance;

        SpAIBinder svcBinder;
        svcBinder = SpAIBinder(AServiceManager_getService(svcName.c_str()));
        auto svc = IVibrator::fromBinder(svcBinder);

        auto ext = waitForService<IVibratorSync>(String16(extName.str().c_str()));

        vibrators.emplace_back(svc, ext);
    }

    auto mgr = ndk::SharedRefBase::make<VibratorManager>(std::move(hwapi), std::move(vibrators));
    binder_status_t status;

    const std::string mgrInst = std::string() + VibratorManager::descriptor + "/" VIBRATOR_NAME;
    status = AServiceManager_addService(mgr->asBinder().get(), mgrInst.c_str());
    LOG_ALWAYS_FATAL_IF(status != STATUS_OK);

    // Only used for callbacks
    ProcessState::self()->setThreadPoolMaxThreadCount(1);
    ProcessState::self()->startThreadPool();

    ABinderProcess_setThreadPoolMaxThreadCount(0);
    ABinderProcess_joinThreadPool();

    return EXIT_FAILURE;  // should not reach
}
