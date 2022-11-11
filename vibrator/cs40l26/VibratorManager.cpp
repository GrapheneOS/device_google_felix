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

#include "VibratorManager.h"

#include <android/hardware/vibrator/BnVibratorSyncCallback.h>
#include <log/log.h>
#include <utils/Trace.h>

#include <numeric>

namespace aidl {
namespace android {
namespace hardware {
namespace vibrator {

const char *kHAPMGRNAME = std::getenv("HAPTIC_MGR_NAME");
#undef ALOGV
#define ALOGV(...) ((void)ALOG(LOG_VERBOSE, kHAPMGRNAME, __VA_ARGS__))
#undef ALOGD
#define ALOGD(...) ((void)ALOG(LOG_DEBUG, kHAPMGRNAME, __VA_ARGS__))
#undef ALOGI
#define ALOGI(...) ((void)ALOG(LOG_INFO, kHAPMGRNAME, __VA_ARGS__))
#undef ALOGW
#define ALOGW(...) ((void)ALOG(LOG_WARN, kHAPMGRNAME, __VA_ARGS__))
#undef ALOGE
#define ALOGE(...) ((void)ALOG(LOG_ERROR, kHAPMGRNAME, __VA_ARGS__))

using ::android::sp;
using ::android::binder::Status;
using ::android::hardware::vibrator::BnVibratorSyncCallback;

class VibratorSyncCallback : public BnVibratorSyncCallback {
  public:
    Status onComplete() override {
        mPromise.set_value();
        return Status::ok();
    }
    auto getFuture() { return mPromise.get_future(); }

  private:
    std::promise<void> mPromise;
};

VibratorManager::VibratorManager(std::unique_ptr<HwApi> hwapi,
                                 const std::vector<VibratorTuple> &&vibrators)
    : mHwApi(std::move(hwapi)), mVibrators(std::move(vibrators)), mAsyncHandle(std::async([] {})) {
    mGPIOStatus = mHwApi->getGPIO();
}

// BnVibratorManager APIs

ndk::ScopedAStatus VibratorManager::getCapabilities(int32_t *_aidl_return) {
    ATRACE_NAME("VibratorManager::getCapabilities");
    int32_t ret =
            IVibratorManager::CAP_SYNC | IVibratorManager::CAP_PREPARE_ON |
            IVibratorManager::CAP_PREPARE_PERFORM | IVibratorManager::CAP_PREPARE_COMPOSE |
            IVibratorManager::CAP_MIXED_TRIGGER_ON | IVibratorManager::CAP_MIXED_TRIGGER_PERFORM |
            IVibratorManager::CAP_MIXED_TRIGGER_COMPOSE | IVibratorManager::CAP_TRIGGER_CALLBACK;

    *_aidl_return = ret;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorManager::getVibratorIds(std::vector<int> *_aidl_return) {
    ATRACE_NAME("VibratorManager::getVibratorIds");
    _aidl_return->resize(mVibrators.size());
    std::iota(_aidl_return->begin(), _aidl_return->end(), 0);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorManager::getVibrator(int vibratorId,
                                                std::shared_ptr<IVibrator> *_aidl_return) {
    ATRACE_NAME("VibratorManager::getVibrator");
    if (!mGPIOStatus) {
        ALOGE("GetVibrator: GPIO status error");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    if (vibratorId >= mVibrators.size()) {
        ALOGE("GetVibrator: wrong requested vibrator ID");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    std::tie(*_aidl_return, std::ignore) = mVibrators.at(vibratorId);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorManager::prepareSynced(const std::vector<int32_t> &ids) {
    ATRACE_NAME("VibratorManager::prepareSynced");

    if (!mGPIOStatus) {
        ALOGE("prepareSynced: GPIO status error");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    if (ids.empty()) {
        ALOGE("PrepareSynced: No vibrator could be synced");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    if (!mSyncContext.empty()) {
        ALOGE("PrepareSynced: mSyncContext is not EMPTY!!!");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    if (isBusy()) {
        ALOGE("PrepareSynced: IS BUSY!!!");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    for (auto &id : ids) {
        auto &[vib, ext] = mVibrators.at(id);
        auto callback = sp<VibratorSyncCallback>::make();

        if (ext->prepareSynced(callback).isOk()) {
            mSyncContext.emplace_back(id, callback->getFuture());
        } else {
            ALOGV("prepareSynced: Fail");
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
    }
    ALOGV("prepareSynced: Done");
    if (mHwApi->initGPIO()) {
        return ndk::ScopedAStatus::ok();
    } else {
        ALOGE("PrepareSynced: GPIO status init fail");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
}

ndk::ScopedAStatus VibratorManager::triggerSynced(
        const std::shared_ptr<IVibratorCallback> &callback) {
    ATRACE_NAME("VibratorManager::triggerSynced");
    ALOGV("TriggerSynced");
    if (isBusy()) {
        ALOGE("TriggerSynced isBusy");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    mHwApi->setTrigger(true);

    doAsync([=]() {
        {
            std::shared_lock lock(mContextMutex);
            for (auto &[id, future] : mSyncContext) {
                future.wait();
            }
        }
        {
            std::unique_lock lock(mContextMutex);
            mSyncContext.clear();
        }
        if (callback) {
            auto ret = callback->onComplete();
            if (!ret.isOk()) {
                ALOGE("Failed completion callback: %d", ret.getExceptionCode());
            }
            ALOGD("Callback in MANAGER onComplete()");
        }
    });

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibratorManager::cancelSynced() {
    ATRACE_NAME("VibratorManager::cancelSynced");

    ALOGV("Do cancelSynced");
    mHwApi->setTrigger(false);
    {
        std::shared_lock lock(mContextMutex);
        for (auto &[id, future] : mSyncContext) {
            auto &[vib, ext] = mVibrators.at(id);
            ext->cancelSynced();
        }
    }
    {
        std::unique_lock lock(mContextMutex);
        mSyncContext.clear();
    }
    mAsyncHandle.wait();

    return ndk::ScopedAStatus::ok();
}

// BnCInterface APIs

binder_status_t VibratorManager::dump(int fd, const char ** /*args*/, uint32_t /*numArgs*/) {
    if (fd < 0) {
        ALOGE("Called debug() with invalid fd.");
        return STATUS_OK;
    }

    mHwApi->debug(fd);

    fsync(fd);

    return STATUS_OK;
}

// Private Methods

template <typename Func, typename... Args>
void VibratorManager::doAsync(Func &&func, Args &&...args) {
    mAsyncHandle = std::async(func, std::forward<Args>(args)...);
}

bool VibratorManager::isBusy() {
    auto timeout = std::chrono::seconds::zero();
    auto status = mAsyncHandle.wait_for(timeout);
    return status != std::future_status::ready;
}

}  // namespace vibrator
}  // namespace hardware
}  // namespace android
}  // namespace aidl
