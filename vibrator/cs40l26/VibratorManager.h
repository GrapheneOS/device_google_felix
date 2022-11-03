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
#pragma once

#include <aidl/android/hardware/vibrator/BnVibrator.h>
#include <aidl/android/hardware/vibrator/BnVibratorManager.h>
#include <android/hardware/vibrator/IVibratorSync.h>

#include <future>
#include <shared_mutex>

namespace aidl {
namespace android {
namespace hardware {
namespace vibrator {

using ::android::hardware::vibrator::IVibratorSync;

class VibratorManager : public BnVibratorManager {
  public:
    // APIs for interfacing with the kernel driver.
    class HwApi {
      public:
        virtual ~HwApi() = default;
        // Get the GPIO pin num and address shift information
        virtual bool getGPIO() = 0;
        // Init the GPIO function
        virtual bool initGPIO() = 0;
        // Trigger activation of the synchronized vibrators.
        virtual bool setTrigger(bool value) = 0;
        // Emit diagnostic information to the given file.
        virtual void debug(int fd) = 0;
    };

    using VibratorTuple = std::tuple<std::shared_ptr<IVibrator>, ::android::sp<IVibratorSync>>;

  public:
    VibratorManager(std::unique_ptr<HwApi> hwapi, const std::vector<VibratorTuple> &&vibrators);

    // BnVibratorManager APIs
    ndk::ScopedAStatus getCapabilities(int32_t *_aidl_return);
    ndk::ScopedAStatus getVibratorIds(std::vector<int> *_aidl_return);
    ndk::ScopedAStatus getVibrator(int vibratorId, std::shared_ptr<IVibrator> *_aidl_return);
    ndk::ScopedAStatus prepareSynced(const std::vector<int32_t> &ids) override;
    ndk::ScopedAStatus triggerSynced(const std::shared_ptr<IVibratorCallback> &callback) override;
    ndk::ScopedAStatus cancelSynced() override;

    // BnCInterface APIs
    binder_status_t dump(int fd, const char **args, uint32_t numArgs) override;

  private:
    template <typename Func, typename... Args>
    void doAsync(Func &&func, Args &&...args);
    bool isBusy();

  private:
    using SyncContext = std::tuple<int32_t, std::future<void>>;

    const std::unique_ptr<HwApi> mHwApi;
    const std::vector<VibratorTuple> mVibrators;
    std::vector<SyncContext> mSyncContext;
    std::shared_mutex mContextMutex;
    std::future<void> mAsyncHandle;
    bool mGPIOStatus;
};

}  // namespace vibrator
}  // namespace hardware
}  // namespace android
}  // namespace aidl
