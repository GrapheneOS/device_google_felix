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

#include <android/hardware/vibrator/BnVibratorSync.h>

#include <array>
#include <fstream>
#include <future>

#include "Vibrator.h"

namespace android {
namespace hardware {
namespace vibrator {

using ::aidl::android::hardware::vibrator::Vibrator;

class VibratorSync : public BnVibratorSync {
  public:
    VibratorSync(std::shared_ptr<Vibrator> vibrator);

    // BnVibratorSync APIs
    binder::Status prepareSynced(const android::sp<IVibratorSyncCallback> &callback) override;
    binder::Status cancelSynced() override;

  private:
    std::shared_ptr<Vibrator> mVibrator;
};

}  // namespace vibrator
}  // namespace hardware
}  // namespace android
