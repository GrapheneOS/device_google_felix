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

#include "Vibrator.h"

#include <glob.h>
#include <hardware/hardware.h>
#include <hardware/vibrator.h>
#include <log/log.h>
#include <stdio.h>
#include <utils/Trace.h>

#include <cinttypes>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof((x)) / sizeof((x)[0]))
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#define LOG_TAG std::getenv("HAPTIC_NAME")
#endif

namespace aidl {
namespace android {
namespace hardware {
namespace vibrator {
static constexpr uint8_t FF_CUSTOM_DATA_LEN = 2;
static constexpr uint16_t FF_CUSTOM_DATA_LEN_MAX_COMP = 2044;  // (COMPOSE_SIZE_MAX + 1) * 8 + 4
static constexpr uint16_t FF_CUSTOM_DATA_LEN_MAX_PWLE = 2302;

static constexpr uint32_t WAVEFORM_DOUBLE_CLICK_SILENCE_MS = 100;

static constexpr uint32_t WAVEFORM_LONG_VIBRATION_THRESHOLD_MS = 50;

static constexpr uint8_t VOLTAGE_SCALE_MAX = 100;

static constexpr int8_t MAX_COLD_START_LATENCY_MS = 6;  // I2C Transaction + DSP Return-From-Standby
static constexpr int8_t MAX_PAUSE_TIMING_ERROR_MS = 1;  // ALERT Irq Handling
static constexpr uint32_t MAX_TIME_MS = UINT16_MAX;
static constexpr float SETTING_TIME_OVERHEAD = 26;  // This time was combined by
                                                    // HAL set the effect to
                                                    // driver and the kernel
                                                    // executes the effect before
                                                    // chip play the effect

static constexpr auto ASYNC_COMPLETION_TIMEOUT = std::chrono::milliseconds(100);
static constexpr auto POLLING_TIMEOUT = 20;
static constexpr int32_t COMPOSE_DELAY_MAX_MS = 10000;

/* nsections is 8 bits. Need to preserve 1 section for the first delay before the first effect. */
static constexpr int32_t COMPOSE_SIZE_MAX = 254;
static constexpr int32_t COMPOSE_PWLE_SIZE_MAX_DEFAULT = 127;

// Measured resonant frequency, f0_measured, is represented by Q10.14 fixed
// point format on cs40l26 devices. The expression to calculate f0 is:
//   f0 = f0_measured / 2^Q14_BIT_SHIFT
// See the LRA Calibration Support documentation for more details.
static constexpr int32_t Q14_BIT_SHIFT = 14;

// Measured Q factor, q_measured, is represented by Q8.16 fixed
// point format on cs40l26 devices. The expression to calculate q is:
//   q = q_measured / 2^Q16_BIT_SHIFT
// See the LRA Calibration Support documentation for more details.
static constexpr int32_t Q16_BIT_SHIFT = 16;

static constexpr int32_t COMPOSE_PWLE_PRIMITIVE_DURATION_MAX_MS = 16383;

static constexpr uint32_t WT_LEN_CALCD = 0x00800000;
static constexpr uint8_t PWLE_CHIRP_BIT = 0x8;  // Dynamic/static frequency and voltage
static constexpr uint8_t PWLE_BRAKE_BIT = 0x4;
static constexpr uint8_t PWLE_AMP_REG_BIT = 0x2;

static constexpr float PWLE_LEVEL_MIN = 0.0;
static constexpr float PWLE_LEVEL_MAX = 1.0;
static constexpr float CS40L26_PWLE_LEVEL_MIX = -1.0;
static constexpr float CS40L26_PWLE_LEVEL_MAX = 0.9995118;
static constexpr float PWLE_FREQUENCY_RESOLUTION_HZ = 1.00;
static constexpr float PWLE_FREQUENCY_MIN_HZ = 1.00;
static constexpr float PWLE_FREQUENCY_MAX_HZ = 1000.00;
static constexpr float PWLE_BW_MAP_SIZE =
        1 + ((PWLE_FREQUENCY_MAX_HZ - PWLE_FREQUENCY_MIN_HZ) / PWLE_FREQUENCY_RESOLUTION_HZ);

/*
 * [15] Edge, 0:Falling, 1:Rising
 * [14:12] GPI_NUM, 1:GPI1 (with CS40L26A, 1 is the only supported GPI)
 * [8] BANK, 0:RAM, 1:R0M
 * [7] USE_BUZZGEN, 0:Not buzzgen, 1:buzzgen
 * [6:0] WAVEFORM_INDEX
 * 0x9100 = 1001 0001 0000 0000: Rising + GPI1 + RAM + Not buzzgen
 */
static constexpr uint16_t GPIO_TRIGGER_CONFIG = 0x9100;

static uint16_t amplitudeToScale(float amplitude, float maximum) {
    float ratio = 100; /* Unit: % */
    if (maximum != 0)
        ratio = amplitude / maximum * 100;

    if (maximum == 0 || ratio > 100)
        ratio = 100;

    return std::round(ratio);
}

enum WaveformBankID : uint8_t {
    RAM_WVFRM_BANK,
    ROM_WVFRM_BANK,
    OWT_WVFRM_BANK,
};

enum WaveformIndex : uint16_t {
    /* Physical waveform */
    WAVEFORM_LONG_VIBRATION_EFFECT_INDEX = 0,
    WAVEFORM_RESERVED_INDEX_1 = 1,
    WAVEFORM_CLICK_INDEX = 2,
    WAVEFORM_SHORT_VIBRATION_EFFECT_INDEX = 3,
    WAVEFORM_THUD_INDEX = 4,
    WAVEFORM_SPIN_INDEX = 5,
    WAVEFORM_QUICK_RISE_INDEX = 6,
    WAVEFORM_SLOW_RISE_INDEX = 7,
    WAVEFORM_QUICK_FALL_INDEX = 8,
    WAVEFORM_LIGHT_TICK_INDEX = 9,
    WAVEFORM_LOW_TICK_INDEX = 10,
    WAVEFORM_RESERVED_MFG_1,
    WAVEFORM_RESERVED_MFG_2,
    WAVEFORM_RESERVED_MFG_3,
    WAVEFORM_MAX_PHYSICAL_INDEX,
    /* OWT waveform */
    WAVEFORM_COMPOSE = WAVEFORM_MAX_PHYSICAL_INDEX,
    WAVEFORM_PWLE,
    /*
     * Refer to <linux/input.h>, the WAVEFORM_MAX_INDEX must not exceed 96.
     * #define FF_GAIN		0x60  // 96 in decimal
     * #define FF_MAX_EFFECTS	FF_GAIN
     */
    WAVEFORM_MAX_INDEX,
};

std::vector<CompositePrimitive> defaultSupportedPrimitives = {
        ndk::enum_range<CompositePrimitive>().begin(), ndk::enum_range<CompositePrimitive>().end()};

enum vibe_state {
    VIBE_STATE_STOPPED = 0,
    VIBE_STATE_HAPTIC,
    VIBE_STATE_ASP,
};

static int min(int x, int y) {
    return x < y ? x : y;
}

static int floatToUint16(float input, uint16_t *output, float scale, float min, float max) {
    if (input < min || input > max)
        return -ERANGE;

    *output = roundf(input * scale);
    return 0;
}

struct dspmem_chunk {
    uint8_t *head;
    uint8_t *current;
    uint8_t *max;
    int bytes;

    uint32_t cache;
    int cachebits;
};

static dspmem_chunk *dspmem_chunk_create(void *data, int size) {
    auto ch = new dspmem_chunk{
            .head = reinterpret_cast<uint8_t *>(data),
            .current = reinterpret_cast<uint8_t *>(data),
            .max = reinterpret_cast<uint8_t *>(data) + size,
    };

    return ch;
}

static bool dspmem_chunk_end(struct dspmem_chunk *ch) {
    return ch->current == ch->max;
}

static int dspmem_chunk_bytes(struct dspmem_chunk *ch) {
    return ch->bytes;
}

static int dspmem_chunk_write(struct dspmem_chunk *ch, int nbits, uint32_t val) {
    int nwrite, i;

    nwrite = min(24 - ch->cachebits, nbits);
    ch->cache <<= nwrite;
    ch->cache |= val >> (nbits - nwrite);
    ch->cachebits += nwrite;
    nbits -= nwrite;

    if (ch->cachebits == 24) {
        if (dspmem_chunk_end(ch))
            return -ENOSPC;

        ch->cache &= 0xFFFFFF;
        for (i = 0; i < sizeof(ch->cache); i++, ch->cache <<= 8)
            *ch->current++ = (ch->cache & 0xFF000000) >> 24;

        ch->bytes += sizeof(ch->cache);
        ch->cachebits = 0;
    }

    if (nbits)
        return dspmem_chunk_write(ch, nbits, val);

    return 0;
}

static int dspmem_chunk_flush(struct dspmem_chunk *ch) {
    if (!ch->cachebits)
        return 0;

    return dspmem_chunk_write(ch, 24 - ch->cachebits, 0);
}

Vibrator::Vibrator(std::unique_ptr<HwApi> hwApiDefault, std::unique_ptr<HwCal> hwCalDefault,
                   std::unique_ptr<HwApi> hwApiDual, std::unique_ptr<HwCal> hwCalDual,
                   std::unique_ptr<HwGPIO> hwgpio)
    : mHwApiDef(std::move(hwApiDefault)),
      mHwCalDef(std::move(hwCalDefault)),
      mHwApiDual(std::move(hwApiDual)),
      mHwCalDual(std::move(hwCalDual)),
      mHwGPIO(std::move(hwgpio)),
      mAsyncHandle(std::async([] {})) {
    int32_t longFrequencyShift;
    std::string caldata{8, '0'};
    uint32_t calVer;

    // ==================Single actuators and dual actuators checking =============================
    if ((mHwApiDual != nullptr) && (mHwCalDual != nullptr))
        mIsDual = true;

    // ==================INPUT Devices== Base =================
    const char *inputEventName = std::getenv("INPUT_EVENT_NAME");
    const char *inputEventPathName = std::getenv("INPUT_EVENT_PATH");
    if ((strstr(inputEventName, "cs40l26") != nullptr) ||
        (strstr(inputEventName, "cs40l26_dual_input") != nullptr)) {
        glob_t inputEventPaths;
        int fd = -1;
        int ret;
        uint32_t val = 0;
        char str[20] = {0x00};
        for (uint8_t retry = 0; retry < 10; retry++) {
            ret = glob(inputEventPathName, 0, nullptr, &inputEventPaths);
            if (ret) {
                ALOGE("Failed to get input event paths (%d): %s", errno, strerror(errno));
            } else {
                for (int i = 0; i < inputEventPaths.gl_pathc; i++) {
                    fd = TEMP_FAILURE_RETRY(open(inputEventPaths.gl_pathv[i], O_RDWR));
                    if (fd > 0) {
                        if (ioctl(fd, EVIOCGBIT(0, sizeof(val)), &val) > 0 &&
                            (val & (1 << EV_FF)) && ioctl(fd, EVIOCGNAME(sizeof(str)), &str) > 0 &&
                            strstr(str, inputEventName) != nullptr) {
                            mInputFd.reset(fd);
                            ALOGI("Control %s through %s", inputEventName,
                                  inputEventPaths.gl_pathv[i]);
                            break;
                        }
                        close(fd);
                    }
                }
            }

            if (ret == 0) {
                globfree(&inputEventPaths);
            }
            if (mInputFd.ok()) {
                break;
            }

            sleep(1);
            ALOGW("Retry #%d to search in %zu input devices.", retry, inputEventPaths.gl_pathc);
        }

        if (!mInputFd.ok()) {
            ALOGE("Failed to get an input event with name %s", inputEventName);
        }
    } else {
        ALOGE("The input name %s is not cs40l26_input or cs40l26_dual_input", inputEventName);
    }

    // ==================INPUT Devices== Flip =================
    if (mIsDual) {
        const char *inputEventNameDual = std::getenv("INPUT_EVENT_NAME_DUAL");
        if ((strstr(inputEventNameDual, "cs40l26_dual_input") != nullptr)) {
            glob_t inputEventPaths;
            int fd = -1;
            int ret;
            uint32_t val = 0;
            char str[20] = {0x00};
            for (uint8_t retry = 0; retry < 10; retry++) {
                ret = glob(inputEventPathName, 0, nullptr, &inputEventPaths);
                if (ret) {
                    ALOGE("Failed to get flip's input event paths (%d): %s", errno,
                          strerror(errno));
                } else {
                    for (int i = 0; i < inputEventPaths.gl_pathc; i++) {
                        fd = TEMP_FAILURE_RETRY(open(inputEventPaths.gl_pathv[i], O_RDWR));
                        if (fd > 0) {
                            if (ioctl(fd, EVIOCGBIT(0, sizeof(val)), &val) > 0 &&
                                (val & (1 << EV_FF)) &&
                                ioctl(fd, EVIOCGNAME(sizeof(str)), &str) > 0 &&
                                strstr(str, inputEventNameDual) != nullptr) {
                                mInputFdDual.reset(fd);
                                ALOGI("Control %s through %s", inputEventNameDual,
                                      inputEventPaths.gl_pathv[i]);
                                break;
                            }
                            close(fd);
                        }
                    }
                }

                if (ret == 0) {
                    globfree(&inputEventPaths);
                }
                if (mInputFdDual.ok()) {
                    break;
                }

                sleep(1);
                ALOGW("Retry #%d to search in %zu input devices.", retry, inputEventPaths.gl_pathc);
            }

            if (!mInputFdDual.ok()) {
                ALOGE("Failed to get an input event with name %s", inputEventNameDual);
            }
            ALOGE("HWAPI: %s", std::getenv("HWAPI_PATH_PREFIX"));
        } else {
            ALOGE("The input name %s is not cs40l26_dual_input", inputEventNameDual);
        }
    }
    // ====================HAL internal effect table== Base ==================================

    mFfEffects.resize(WAVEFORM_MAX_INDEX);
    mEffectDurations.resize(WAVEFORM_MAX_INDEX);
    mEffectDurations = {
            1000, 100, 32, 1000, 300, 130, 150, 500, 100, 10, 12, 1000, 1000, 1000,
    }; /* 11+3 waveforms. The duration must < UINT16_MAX */

    uint8_t effectIndex;
    for (effectIndex = 0; effectIndex < WAVEFORM_MAX_INDEX; effectIndex++) {
        if (effectIndex < WAVEFORM_MAX_PHYSICAL_INDEX) {
            /* Initialize physical waveforms. */
            mFfEffects[effectIndex] = {
                    .type = FF_PERIODIC,
                    .id = -1,
                    .replay.length = static_cast<uint16_t>(mEffectDurations[effectIndex]),
                    .u.periodic.waveform = FF_CUSTOM,
                    .u.periodic.custom_data = new int16_t[2]{RAM_WVFRM_BANK, effectIndex},
                    .u.periodic.custom_len = FF_CUSTOM_DATA_LEN,
            };
            // Bypass the waveform update due to different input name
            if ((strstr(inputEventName, "cs40l26") != nullptr) ||
                (strstr(inputEventName, "cs40l26_dual_input") != nullptr)) {
                if (!mHwApiDef->setFFEffect(
                            mInputFd, &mFfEffects[effectIndex],
                            static_cast<uint16_t>(mFfEffects[effectIndex].replay.length))) {
                    ALOGE("Failed upload effect %d (%d): %s", effectIndex, errno, strerror(errno));
                }
            }
            if (mFfEffects[effectIndex].id != effectIndex) {
                ALOGW("Unexpected effect index: %d -> %d", effectIndex, mFfEffects[effectIndex].id);
            }
        } else {
            /* Initiate placeholders for OWT effects. */
            mFfEffects[effectIndex] = {
                    .type = FF_PERIODIC,
                    .id = -1,
                    .replay.length = 0,
                    .u.periodic.waveform = FF_CUSTOM,
                    .u.periodic.custom_data = nullptr,
                    .u.periodic.custom_len = 0,
            };
        }
    }

    // ====================HAL internal effect table== Flip ==================================
    if (mIsDual) {
        mFfEffectsDual.resize(WAVEFORM_MAX_INDEX);

        for (effectIndex = 0; effectIndex < WAVEFORM_MAX_INDEX; effectIndex++) {
            if (effectIndex < WAVEFORM_MAX_PHYSICAL_INDEX) {
                /* Initialize physical waveforms. */
                mFfEffectsDual[effectIndex] = {
                        .type = FF_PERIODIC,
                        .id = -1,
                        .replay.length = static_cast<uint16_t>(mEffectDurations[effectIndex]),
                        .u.periodic.waveform = FF_CUSTOM,
                        .u.periodic.custom_data = new int16_t[2]{RAM_WVFRM_BANK, effectIndex},
                        .u.periodic.custom_len = FF_CUSTOM_DATA_LEN,
                };
                // Bypass the waveform update due to different input name
                if ((strstr(inputEventName, "cs40l26") != nullptr) ||
                    (strstr(inputEventName, "cs40l26_dual_input") != nullptr)) {
                    if (!mHwApiDual->setFFEffect(
                                mInputFdDual, &mFfEffectsDual[effectIndex],
                                static_cast<uint16_t>(mFfEffectsDual[effectIndex].replay.length))) {
                        ALOGE("Failed upload flip's effect %d (%d): %s", effectIndex, errno,
                              strerror(errno));
                    }
                }
                if (mFfEffectsDual[effectIndex].id != effectIndex) {
                    ALOGW("Unexpected effect index: %d -> %d", effectIndex,
                          mFfEffectsDual[effectIndex].id);
                }
            } else {
                /* Initiate placeholders for OWT effects. */
                mFfEffectsDual[effectIndex] = {
                        .type = FF_PERIODIC,
                        .id = -1,
                        .replay.length = 0,
                        .u.periodic.waveform = FF_CUSTOM,
                        .u.periodic.custom_data = nullptr,
                        .u.periodic.custom_len = 0,
                };
            }
        }
    }
    // ==============Calibration data checking======================================

    if (mHwCalDef->getF0(&caldata)) {
        mHwApiDef->setF0(caldata);
    }
    if (mHwCalDef->getRedc(&caldata)) {
        mHwApiDef->setRedc(caldata);
    }
    if (mHwCalDef->getQ(&caldata)) {
        mHwApiDef->setQ(caldata);
    }

    if (mHwCalDef->getF0SyncOffset(&mF0Offset)) {
        ALOGD("Vibrator::Vibrator: F0 offset calculated from both base and flip calibration data: "
              "%u",
              mF0Offset);
    } else {
        mHwCalDef->getLongFrequencyShift(&longFrequencyShift);
        if (longFrequencyShift > 0) {
            mF0Offset = longFrequencyShift * std::pow(2, 14);
        } else if (longFrequencyShift < 0) {
            mF0Offset = std::pow(2, 24) - std::abs(longFrequencyShift) * std::pow(2, 14);
        } else {
            mF0Offset = 0;
        }
        ALOGD("Vibrator::Vibrator: F0 offset calculated from long shift frequency: %u", mF0Offset);
    }

    if (mIsDual) {
        if (mHwCalDual->getF0(&caldata)) {
            mHwApiDual->setF0(caldata);
        }
        if (mHwCalDual->getRedc(&caldata)) {
            mHwApiDual->setRedc(caldata);
        }
        if (mHwCalDual->getQ(&caldata)) {
            mHwApiDual->setQ(caldata);
        }

        if (mHwCalDual->getF0SyncOffset(&mF0OffsetDual)) {
            ALOGD("Vibrator::Vibrator: Dual: F0 offset calculated from both base and flip "
                  "calibration data: "
                  "%u",
                  mF0OffsetDual);
        }
    }

    mHwCalDef->getVersion(&calVer);
    if (calVer == 2) {
        mHwCalDef->getTickVolLevels(&(mTickEffectVol));
        mHwCalDef->getClickVolLevels(&(mClickEffectVol));
        mHwCalDef->getLongVolLevels(&(mLongEffectVol));
    } else {
        ALOGW("Unsupported calibration version! Using the default calibration value");
        mHwCalDef->getTickVolLevels(&(mTickEffectVol));
        mHwCalDef->getClickVolLevels(&(mClickEffectVol));
        mHwCalDef->getLongVolLevels(&(mLongEffectVol));
    }

    // ================Project specific setting to driver===============================

    mHwApiDef->setF0CompEnable(mHwCalDef->isF0CompEnabled());
    mHwApiDef->setRedcCompEnable(mHwCalDef->isRedcCompEnabled());
    mHwApiDef->setMinOnOffInterval(MIN_ON_OFF_INTERVAL_US);
    if (mIsDual) {
        mHwApiDual->setF0CompEnable(mHwCalDual->isF0CompEnabled());
        mHwApiDual->setRedcCompEnable(mHwCalDual->isRedcCompEnabled());
        mHwApiDual->setMinOnOffInterval(MIN_ON_OFF_INTERVAL_US);
    }
    // ===============Audio coupled haptics bool init ========
    mIsUnderExternalControl = false;

    // =============== Compose PWLE check =====================================
    mIsChirpEnabled = mHwCalDef->isChirpEnabled();

    mHwCalDef->getSupportedPrimitives(&mSupportedPrimitivesBits);
    if (mSupportedPrimitivesBits > 0) {
        for (auto e : defaultSupportedPrimitives) {
            if (mSupportedPrimitivesBits & (1 << uint32_t(e))) {
                mSupportedPrimitives.emplace_back(e);
            }
        }
    } else {
        for (auto e : defaultSupportedPrimitives) {
            mSupportedPrimitivesBits |= (1 << uint32_t(e));
        }
        mSupportedPrimitives = defaultSupportedPrimitives;
    }

    // ====== Get GPIO status and init it ================
    mGPIOStatus = mHwGPIO->getGPIO();
    if (!mGPIOStatus || !mHwGPIO->initGPIO()) {
        ALOGE("Vibrator: GPIO initialization process error");
    }
}

ndk::ScopedAStatus Vibrator::getCapabilities(int32_t *_aidl_return) {
    ATRACE_NAME("Vibrator::getCapabilities");

    int32_t ret = IVibrator::CAP_ON_CALLBACK | IVibrator::CAP_PERFORM_CALLBACK |
                  IVibrator::CAP_AMPLITUDE_CONTROL | IVibrator::CAP_GET_RESONANT_FREQUENCY |
                  IVibrator::CAP_GET_Q_FACTOR;
    if (hasHapticAlsaDevice()) {
        ret |= IVibrator::CAP_EXTERNAL_CONTROL;
    } else {
        ALOGE("No haptics ALSA device");
    }
    if (mHwApiDef->hasOwtFreeSpace()) {
        ret |= IVibrator::CAP_COMPOSE_EFFECTS;
        if (mIsChirpEnabled) {
            ret |= IVibrator::CAP_FREQUENCY_CONTROL | IVibrator::CAP_COMPOSE_PWLE_EFFECTS;
        }
    }
    *_aidl_return = ret;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::off() {
    ATRACE_NAME("Vibrator::off");
    bool ret{true};
    const std::scoped_lock<std::mutex> lock(mActiveId_mutex);

    if (mActiveId >= 0) {
        ALOGD("Off: Stop the active effect: %d", mActiveId);
        /* Stop the active effect. */
        if (!mHwApiDef->setFFPlay(mInputFd, mActiveId, false)) {
            ALOGE("Off: Failed to stop effect %d (%d): %s", mActiveId, errno, strerror(errno));
            ret = false;
        }
        if (mIsDual && (!mHwApiDual->setFFPlay(mInputFdDual, mActiveId, false))) {
            ALOGE("Off: Failed to stop flip's effect %d (%d): %s", mActiveId, errno,
                  strerror(errno));
            ret = false;
        }
        /* Do erase process */
        if ((mActiveId >= WAVEFORM_MAX_PHYSICAL_INDEX) &&
            (!mHwApiDef->eraseOwtEffect(mInputFd, mActiveId, &mFfEffects))) {
            ALOGE("Off: Failed to clean up the composed effect %d", mActiveId);
            ret = false;
        }

        if (mIsDual && (mActiveId >= WAVEFORM_MAX_PHYSICAL_INDEX) &&
            (!mHwApiDual->eraseOwtEffect(mInputFdDual, mActiveId, &mFfEffectsDual))) {
            ALOGE("Off: Failed to clean up flip's the composed effect %d", mActiveId);
            ret = false;
        }
        if (!mHwGPIO->setGPIOOutput(false)) {
            ALOGE("Off: Failed to reset GPIO(%d): %s", errno, strerror(errno));
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
    } else {
        ALOGD("Off: Vibrator is already off");
    }

    setGlobalAmplitude(false);
    if (mF0Offset) {
        mHwApiDef->setF0Offset(0);
        if (mIsDual && mF0OffsetDual) {
            mHwApiDual->setF0Offset(0);
        }
    }

    if (ret) {
        ALOGD("Off: Done.");
        mActiveId = -1;
        return ndk::ScopedAStatus::ok();
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
}

ndk::ScopedAStatus Vibrator::on(int32_t timeoutMs,
                                const std::shared_ptr<IVibratorCallback> &callback) {
    ATRACE_NAME("Vibrator::on");
    ALOGD("Vibrator::on");

    if (timeoutMs > MAX_TIME_MS) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    const uint16_t index = (timeoutMs < WAVEFORM_LONG_VIBRATION_THRESHOLD_MS)
                                   ? WAVEFORM_SHORT_VIBRATION_EFFECT_INDEX
                                   : WAVEFORM_LONG_VIBRATION_EFFECT_INDEX;
    if (MAX_COLD_START_LATENCY_MS <= MAX_TIME_MS - timeoutMs) {
        timeoutMs += MAX_COLD_START_LATENCY_MS;
    }
    setGlobalAmplitude(true);
    if (mF0Offset) {
        mHwApiDef->setF0Offset(mF0Offset);
        if (mIsDual && mF0OffsetDual) {
            mHwApiDual->setF0Offset(mF0OffsetDual);
        }
    }
    return on(timeoutMs, index, nullptr /*ignored*/, callback);
}

ndk::ScopedAStatus Vibrator::perform(Effect effect, EffectStrength strength,
                                     const std::shared_ptr<IVibratorCallback> &callback,
                                     int32_t *_aidl_return) {
    ATRACE_NAME("Vibrator::perform");
    ALOGD("Vibrator::perform");
    return performEffect(effect, strength, callback, _aidl_return);
}

ndk::ScopedAStatus Vibrator::getSupportedEffects(std::vector<Effect> *_aidl_return) {
    *_aidl_return = {Effect::TEXTURE_TICK, Effect::TICK, Effect::CLICK, Effect::HEAVY_CLICK,
                     Effect::DOUBLE_CLICK};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::setAmplitude(float amplitude) {
    ATRACE_NAME("Vibrator::setAmplitude");

    if (amplitude <= 0.0f || amplitude > 1.0f) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    mLongEffectScale = amplitude;
    if (!isUnderExternalControl()) {
        return setGlobalAmplitude(true);
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
}

ndk::ScopedAStatus Vibrator::setExternalControl(bool enabled) {
    ATRACE_NAME("Vibrator::setExternalControl");

    setGlobalAmplitude(enabled);

    if (mHasHapticAlsaDevice || mConfigHapticAlsaDeviceDone || hasHapticAlsaDevice()) {
        if (!mHwApiDef->setHapticPcmAmp(&mHapticPcm, enabled, mCard, mDevice)) {
            ALOGE("Failed to %s haptic pcm device: %d", (enabled ? "enable" : "disable"), mDevice);
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
    } else {
        ALOGE("No haptics ALSA device");
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    mIsUnderExternalControl = enabled;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getCompositionDelayMax(int32_t *maxDelayMs) {
    ATRACE_NAME("Vibrator::getCompositionDelayMax");
    *maxDelayMs = COMPOSE_DELAY_MAX_MS;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getCompositionSizeMax(int32_t *maxSize) {
    ATRACE_NAME("Vibrator::getCompositionSizeMax");
    *maxSize = COMPOSE_SIZE_MAX;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getSupportedPrimitives(std::vector<CompositePrimitive> *supported) {
    *supported = mSupportedPrimitives;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getPrimitiveDuration(CompositePrimitive primitive,
                                                  int32_t *durationMs) {
    ndk::ScopedAStatus status;
    uint32_t effectIndex;
    if (primitive != CompositePrimitive::NOOP) {
        status = getPrimitiveDetails(primitive, &effectIndex);
        if (!status.isOk()) {
            return status;
        }
        // Please check the overhead time detail in b/261841035
        *durationMs = mEffectDurations[effectIndex] + SETTING_TIME_OVERHEAD;
    } else {
        *durationMs = 0;
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::compose(const std::vector<CompositeEffect> &composite,
                                     const std::shared_ptr<IVibratorCallback> &callback) {
    ATRACE_NAME("Vibrator::compose");
    ALOGD("Vibrator::compose");
    uint16_t size;
    uint16_t nextEffectDelay;
    uint16_t totalDuration = 0;

    auto ch = dspmem_chunk_create(new uint8_t[FF_CUSTOM_DATA_LEN_MAX_COMP]{0x00},
                                  FF_CUSTOM_DATA_LEN_MAX_COMP);

    if (composite.size() > COMPOSE_SIZE_MAX || composite.empty()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    /* Check if there is a wait before the first effect. */
    nextEffectDelay = composite.front().delayMs;
    totalDuration += nextEffectDelay;
    if (nextEffectDelay > COMPOSE_DELAY_MAX_MS || nextEffectDelay < 0) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    } else if (nextEffectDelay > 0) {
        size = composite.size() + 1;
    } else {
        size = composite.size();
    }

    dspmem_chunk_write(ch, 8, 0);                      /* Padding */
    dspmem_chunk_write(ch, 8, (uint8_t)(0xFF & size)); /* nsections */
    dspmem_chunk_write(ch, 8, 0);                      /* repeat */
    uint8_t header_count = dspmem_chunk_bytes(ch);

    /* Insert 1 section for a wait before the first effect. */
    if (nextEffectDelay) {
        dspmem_chunk_write(ch, 32, 0); /* amplitude, index, repeat & flags */
        dspmem_chunk_write(ch, 16, (uint16_t)(0xFFFF & nextEffectDelay)); /* delay */
    }

    for (uint32_t i_curr = 0, i_next = 1; i_curr < composite.size(); i_curr++, i_next++) {
        auto &e_curr = composite[i_curr];
        uint32_t effectIndex = 0;
        uint32_t effectVolLevel = 0;
        if (e_curr.scale < 0.0f || e_curr.scale > 1.0f) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }

        if (e_curr.primitive != CompositePrimitive::NOOP) {
            ndk::ScopedAStatus status;
            status = getPrimitiveDetails(e_curr.primitive, &effectIndex);
            if (!status.isOk()) {
                return status;
            }
            effectVolLevel = intensityToVolLevel(e_curr.scale, effectIndex);
            totalDuration += mEffectDurations[effectIndex];
        }

        /* Fetch the next composite effect delay and fill into the current section */
        nextEffectDelay = 0;
        if (i_next < composite.size()) {
            auto &e_next = composite[i_next];
            int32_t delay = e_next.delayMs;

            if (delay > COMPOSE_DELAY_MAX_MS || delay < 0) {
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
            nextEffectDelay = delay;
            totalDuration += delay;
        }

        if (effectIndex == 0 && nextEffectDelay == 0) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }

        dspmem_chunk_write(ch, 8, (uint8_t)(0xFF & effectVolLevel));      /* amplitude */
        dspmem_chunk_write(ch, 8, (uint8_t)(0xFF & effectIndex));         /* index */
        dspmem_chunk_write(ch, 8, 0);                                     /* repeat */
        dspmem_chunk_write(ch, 8, 0);                                     /* flags */
        dspmem_chunk_write(ch, 16, (uint16_t)(0xFFFF & nextEffectDelay)); /* delay */
    }
    dspmem_chunk_flush(ch);
    if (header_count == dspmem_chunk_bytes(ch)) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    } else {
        mFfEffects[WAVEFORM_COMPOSE].replay.length = totalDuration;
        if (mIsDual) {
            mFfEffectsDual[WAVEFORM_COMPOSE].replay.length = totalDuration;
        }
        return performEffect(WAVEFORM_MAX_INDEX /*ignored*/, VOLTAGE_SCALE_MAX /*ignored*/, ch,
                             callback);
    }
}

ndk::ScopedAStatus Vibrator::on(uint32_t timeoutMs, uint32_t effectIndex, dspmem_chunk *ch,
                                const std::shared_ptr<IVibratorCallback> &callback) {
    ndk::ScopedAStatus status = ndk::ScopedAStatus::ok();

    if (effectIndex >= FF_MAX_EFFECTS) {
        ALOGE("Invalid waveform index %d", effectIndex);
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (mAsyncHandle.wait_for(ASYNC_COMPLETION_TIMEOUT) != std::future_status::ready) {
        ALOGE("Previous vibration pending: prev: %d, curr: %d", mActiveId, effectIndex);
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    if (ch) {
        /* Upload OWT effect. */
        if (ch->head == nullptr) {
            ALOGE("Invalid OWT bank");
            delete ch;
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        bool isPwle = (*reinterpret_cast<uint16_t *>(ch->head) != 0x0000);
        effectIndex = isPwle ? WAVEFORM_PWLE : WAVEFORM_COMPOSE;

        uint32_t freeBytes;
        mHwApiDef->getOwtFreeSpace(&freeBytes);
        if (dspmem_chunk_bytes(ch) > freeBytes) {
            ALOGE("Invalid OWT length: Effect %d: %d > %d!", effectIndex, dspmem_chunk_bytes(ch),
                  freeBytes);
            delete ch;
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
        if (mIsDual) {
            mHwApiDual->getOwtFreeSpace(&freeBytes);
            if (dspmem_chunk_bytes(ch) > freeBytes) {
                ALOGE("Invalid OWT length in flip: Effect %d: %d > %d!", effectIndex,
                      dspmem_chunk_bytes(ch), freeBytes);
                delete ch;
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
            }
        }

        int errorStatus;
        if (mGPIOStatus && mIsDual) {
            mFfEffects[effectIndex].trigger.button = GPIO_TRIGGER_CONFIG | effectIndex;
            mFfEffectsDual[effectIndex].trigger.button = GPIO_TRIGGER_CONFIG | effectIndex;
        } else {
            ALOGD("Not dual haptics HAL and GPIO status fail");
        }

        if (!mHwApiDef->uploadOwtEffect(mInputFd, ch->head, dspmem_chunk_bytes(ch),
                                        &mFfEffects[effectIndex], &effectIndex, &errorStatus)) {
            delete ch;
            ALOGE("Invalid uploadOwtEffect");
            return ndk::ScopedAStatus::fromExceptionCode(errorStatus);
        }
        if (mIsDual && !mHwApiDual->uploadOwtEffect(mInputFdDual, ch->head, dspmem_chunk_bytes(ch),
                                                    &mFfEffectsDual[effectIndex], &effectIndex,
                                                    &errorStatus)) {
            delete ch;
            ALOGE("Invalid uploadOwtEffect in flip");
            return ndk::ScopedAStatus::fromExceptionCode(errorStatus);
        }
        delete ch;

    } else if (effectIndex == WAVEFORM_SHORT_VIBRATION_EFFECT_INDEX ||
               effectIndex == WAVEFORM_LONG_VIBRATION_EFFECT_INDEX) {
        /* Update duration for long/short vibration. */
        mFfEffects[effectIndex].replay.length = static_cast<uint16_t>(timeoutMs);
        if (mGPIOStatus && mIsDual) {
            mFfEffects[effectIndex].trigger.button = GPIO_TRIGGER_CONFIG | effectIndex;
            mFfEffectsDual[effectIndex].trigger.button = GPIO_TRIGGER_CONFIG | effectIndex;
        } else {
            ALOGD("Not dual haptics HAL and GPIO status fail");
        }
        if (!mHwApiDef->setFFEffect(mInputFd, &mFfEffects[effectIndex],
                                    static_cast<uint16_t>(timeoutMs))) {
            ALOGE("Failed to edit effect %d (%d): %s", effectIndex, errno, strerror(errno));
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        if (mIsDual) {
            mFfEffectsDual[effectIndex].replay.length = static_cast<uint16_t>(timeoutMs);
            if (!mHwApiDual->setFFEffect(mInputFdDual, &mFfEffectsDual[effectIndex],
                                         static_cast<uint16_t>(timeoutMs))) {
                ALOGE("Failed to edit flip's effect %d (%d): %s", effectIndex, errno,
                      strerror(errno));
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
        }
    }

    {
        const std::scoped_lock<std::mutex> lock(mActiveId_mutex);
        /* Play the event now. */
        mActiveId = effectIndex;
        if (!mGPIOStatus) {
            ALOGE("GetVibrator: GPIO status error");
            // Do playcode to play effect
            if (!mHwApiDef->setFFPlay(mInputFd, effectIndex, true)) {
                ALOGE("Failed to play effect %d (%d): %s", effectIndex, errno, strerror(errno));
                mActiveId = -1;
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
            if (mIsDual && !mHwApiDual->setFFPlay(mInputFdDual, effectIndex, true)) {
                ALOGE("Failed to play flip's effect %d (%d): %s", effectIndex, errno,
                      strerror(errno));
                mActiveId = -1;
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
        } else {
            // Using GPIO to play effect
            if ((effectIndex == WAVEFORM_CLICK_INDEX || effectIndex == WAVEFORM_LIGHT_TICK_INDEX)) {
                mFfEffects[effectIndex].trigger.button = GPIO_TRIGGER_CONFIG | effectIndex;
                if (!mHwApiDef->setFFEffect(mInputFd, &mFfEffects[effectIndex],
                                            mFfEffects[effectIndex].replay.length)) {
                    ALOGE("Failed to edit effect %d (%d): %s", effectIndex, errno, strerror(errno));
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
                }
                if (mIsDual) {
                    mFfEffectsDual[effectIndex].trigger.button = GPIO_TRIGGER_CONFIG | effectIndex;
                    if (!mHwApiDual->setFFEffect(mInputFdDual, &mFfEffectsDual[effectIndex],
                                                 mFfEffectsDual[effectIndex].replay.length)) {
                        ALOGE("Failed to edit flip's effect %d (%d): %s", effectIndex, errno,
                              strerror(errno));
                        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
                    }
                }
            }
            if (!mHwGPIO->setGPIOOutput(true)) {
                ALOGE("Failed to trigger effect %d (%d) by GPIO: %s", effectIndex, errno,
                      strerror(errno));
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
        }
    }

    mAsyncHandle = std::async(&Vibrator::waitForComplete, this, callback);
    ALOGD("Vibrator::on, set done.");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::setEffectAmplitude(float amplitude, float maximum) {
    uint16_t scale = amplitudeToScale(amplitude, maximum);
    if (!mHwApiDef->setFFGain(mInputFd, scale)) {
        ALOGE("Failed to set the gain to %u (%d): %s", scale, errno, strerror(errno));
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    if (mIsDual) {
        if (!mHwApiDual->setFFGain(mInputFdDual, scale)) {
            ALOGE("Failed to set flip's gain to %u (%d): %s", scale, errno, strerror(errno));
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::setGlobalAmplitude(bool set) {
    uint8_t amplitude = set ? roundf(mLongEffectScale * mLongEffectVol[1]) : VOLTAGE_SCALE_MAX;
    if (!set) {
        mLongEffectScale = 1.0;  // Reset the scale for the later new effect.
    }

    return setEffectAmplitude(amplitude, VOLTAGE_SCALE_MAX);
}

ndk::ScopedAStatus Vibrator::getSupportedAlwaysOnEffects(std::vector<Effect> * /*_aidl_return*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::alwaysOnEnable(int32_t /*id*/, Effect /*effect*/,
                                            EffectStrength /*strength*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}
ndk::ScopedAStatus Vibrator::alwaysOnDisable(int32_t /*id*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getResonantFrequency(float *resonantFreqHz) {
    std::string caldata{8, '0'};
    if (!mHwCalDef->getF0(&caldata)) {
        ALOGE("Failed to get resonant frequency (%d): %s", errno, strerror(errno));
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    *resonantFreqHz = static_cast<float>(std::stoul(caldata, nullptr, 16)) / (1 << Q14_BIT_SHIFT);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getQFactor(float *qFactor) {
    std::string caldata{8, '0'};
    if (!mHwCalDef->getQ(&caldata)) {
        ALOGE("Failed to get q factor (%d): %s", errno, strerror(errno));
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    *qFactor = static_cast<float>(std::stoul(caldata, nullptr, 16)) / (1 << Q16_BIT_SHIFT);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getFrequencyResolution(float *freqResolutionHz) {
    int32_t capabilities;
    Vibrator::getCapabilities(&capabilities);
    if (capabilities & IVibrator::CAP_FREQUENCY_CONTROL) {
        *freqResolutionHz = PWLE_FREQUENCY_RESOLUTION_HZ;
        return ndk::ScopedAStatus::ok();
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
}

ndk::ScopedAStatus Vibrator::getFrequencyMinimum(float *freqMinimumHz) {
    int32_t capabilities;
    Vibrator::getCapabilities(&capabilities);
    if (capabilities & IVibrator::CAP_FREQUENCY_CONTROL) {
        *freqMinimumHz = PWLE_FREQUENCY_MIN_HZ;
        return ndk::ScopedAStatus::ok();
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
}

ndk::ScopedAStatus Vibrator::getBandwidthAmplitudeMap(std::vector<float> *_aidl_return) {
    // TODO(b/170919640): complete implementation
    int32_t capabilities;
    Vibrator::getCapabilities(&capabilities);
    if (capabilities & IVibrator::CAP_FREQUENCY_CONTROL) {
        std::vector<float> bandwidthAmplitudeMap(PWLE_BW_MAP_SIZE, 1.0);
        *_aidl_return = bandwidthAmplitudeMap;
        return ndk::ScopedAStatus::ok();
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
}

ndk::ScopedAStatus Vibrator::getPwlePrimitiveDurationMax(int32_t *durationMs) {
    int32_t capabilities;
    Vibrator::getCapabilities(&capabilities);
    if (capabilities & IVibrator::CAP_COMPOSE_PWLE_EFFECTS) {
        *durationMs = COMPOSE_PWLE_PRIMITIVE_DURATION_MAX_MS;
        return ndk::ScopedAStatus::ok();
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
}

ndk::ScopedAStatus Vibrator::getPwleCompositionSizeMax(int32_t *maxSize) {
    int32_t capabilities;
    Vibrator::getCapabilities(&capabilities);
    if (capabilities & IVibrator::CAP_COMPOSE_PWLE_EFFECTS) {
        *maxSize = COMPOSE_PWLE_SIZE_MAX_DEFAULT;
        return ndk::ScopedAStatus::ok();
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
}

ndk::ScopedAStatus Vibrator::getSupportedBraking(std::vector<Braking> *supported) {
    int32_t capabilities;
    Vibrator::getCapabilities(&capabilities);
    if (capabilities & IVibrator::CAP_COMPOSE_PWLE_EFFECTS) {
        *supported = {
                Braking::NONE,
        };
        return ndk::ScopedAStatus::ok();
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
}

static void resetPreviousEndAmplitudeEndFrequency(float *prevEndAmplitude,
                                                  float *prevEndFrequency) {
    const float reset = -1.0;
    *prevEndAmplitude = reset;
    *prevEndFrequency = reset;
}

static void incrementIndex(int *index) {
    *index += 1;
}

static void constructPwleSegment(dspmem_chunk *ch, uint16_t delay, uint16_t amplitude,
                                 uint16_t frequency, uint8_t flags, uint32_t vbemfTarget = 0) {
    dspmem_chunk_write(ch, 16, delay);
    dspmem_chunk_write(ch, 12, amplitude);
    dspmem_chunk_write(ch, 12, frequency);
    /* feature flags to control the chirp, CLAB braking, back EMF amplitude regulation */
    dspmem_chunk_write(ch, 8, (flags | 1) << 4);
    if (flags & PWLE_AMP_REG_BIT) {
        dspmem_chunk_write(ch, 24, vbemfTarget); /* target back EMF voltage */
    }
}

static int constructActiveSegment(dspmem_chunk *ch, int duration, float amplitude, float frequency,
                                  bool chirp) {
    uint16_t delay = 0;
    uint16_t amp = 0;
    uint16_t freq = 0;
    uint8_t flags = 0x0;
    if ((floatToUint16(duration, &delay, 4, 0.0f, COMPOSE_PWLE_PRIMITIVE_DURATION_MAX_MS) < 0) ||
        (floatToUint16(amplitude, &amp, 2048, CS40L26_PWLE_LEVEL_MIX, CS40L26_PWLE_LEVEL_MAX) <
         0) ||
        (floatToUint16(frequency, &freq, 4, PWLE_FREQUENCY_MIN_HZ, PWLE_FREQUENCY_MAX_HZ) < 0)) {
        ALOGE("Invalid argument: %d, %f, %f", duration, amplitude, frequency);
        return -ERANGE;
    }
    if (chirp) {
        flags |= PWLE_CHIRP_BIT;
    }
    constructPwleSegment(ch, delay, amp, freq, flags, 0 /*ignored*/);
    return 0;
}

static int constructBrakingSegment(dspmem_chunk *ch, int duration, Braking brakingType) {
    uint16_t delay = 0;
    uint16_t freq = 0;
    uint8_t flags = 0x00;
    if (floatToUint16(duration, &delay, 4, 0.0f, COMPOSE_PWLE_PRIMITIVE_DURATION_MAX_MS) < 0) {
        ALOGE("Invalid argument: %d", duration);
        return -ERANGE;
    }
    floatToUint16(PWLE_FREQUENCY_MIN_HZ, &freq, 4, PWLE_FREQUENCY_MIN_HZ, PWLE_FREQUENCY_MAX_HZ);
    if (static_cast<std::underlying_type<Braking>::type>(brakingType)) {
        flags |= PWLE_BRAKE_BIT;
    }

    constructPwleSegment(ch, delay, 0 /*ignored*/, freq, flags, 0 /*ignored*/);
    return 0;
}

static void updateWLength(dspmem_chunk *ch, uint32_t totalDuration) {
    totalDuration *= 8;            /* Unit: 0.125 ms (since wlength played @ 8kHz). */
    totalDuration |= WT_LEN_CALCD; /* Bit 23 is for WT_LEN_CALCD; Bit 22 is for WT_INDEFINITE. */
    *(ch->head + 0) = (totalDuration >> 24) & 0xFF;
    *(ch->head + 1) = (totalDuration >> 16) & 0xFF;
    *(ch->head + 2) = (totalDuration >> 8) & 0xFF;
    *(ch->head + 3) = totalDuration & 0xFF;
}

static void updateNSection(dspmem_chunk *ch, int segmentIdx) {
    *(ch->head + 7) |= (0xF0 & segmentIdx) >> 4; /* Bit 4 to 7 */
    *(ch->head + 9) |= (0x0F & segmentIdx) << 4; /* Bit 3 to 0 */
}

ndk::ScopedAStatus Vibrator::composePwle(const std::vector<PrimitivePwle> &composite,
                                         const std::shared_ptr<IVibratorCallback> &callback) {
    ATRACE_NAME("Vibrator::composePwle");
    int32_t capabilities;

    Vibrator::getCapabilities(&capabilities);
    if ((capabilities & IVibrator::CAP_COMPOSE_PWLE_EFFECTS) == 0) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    if (composite.empty() || composite.size() > COMPOSE_PWLE_SIZE_MAX_DEFAULT) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    std::vector<Braking> supported;
    Vibrator::getSupportedBraking(&supported);
    bool isClabSupported =
            std::find(supported.begin(), supported.end(), Braking::CLAB) != supported.end();

    int segmentIdx = 0;
    uint32_t totalDuration = 0;
    float prevEndAmplitude;
    float prevEndFrequency;
    resetPreviousEndAmplitudeEndFrequency(&prevEndAmplitude, &prevEndFrequency);
    auto ch = dspmem_chunk_create(new uint8_t[FF_CUSTOM_DATA_LEN_MAX_PWLE]{0x00},
                                  FF_CUSTOM_DATA_LEN_MAX_PWLE);
    bool chirp = false;

    dspmem_chunk_write(ch, 24, 0x000000); /* Waveform length placeholder */
    dspmem_chunk_write(ch, 8, 0);         /* Repeat */
    dspmem_chunk_write(ch, 12, 0);        /* Wait time between repeats */
    dspmem_chunk_write(ch, 8, 0x00);      /* nsections placeholder */

    for (auto &e : composite) {
        switch (e.getTag()) {
            case PrimitivePwle::active: {
                auto active = e.get<PrimitivePwle::active>();
                if (active.duration < 0 ||
                    active.duration > COMPOSE_PWLE_PRIMITIVE_DURATION_MAX_MS) {
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
                }
                if (active.startAmplitude < PWLE_LEVEL_MIN ||
                    active.startAmplitude > PWLE_LEVEL_MAX ||
                    active.endAmplitude < PWLE_LEVEL_MIN || active.endAmplitude > PWLE_LEVEL_MAX) {
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
                }
                if (active.startAmplitude > CS40L26_PWLE_LEVEL_MAX) {
                    active.startAmplitude = CS40L26_PWLE_LEVEL_MAX;
                }
                if (active.endAmplitude > CS40L26_PWLE_LEVEL_MAX) {
                    active.endAmplitude = CS40L26_PWLE_LEVEL_MAX;
                }

                if (active.startFrequency < PWLE_FREQUENCY_MIN_HZ ||
                    active.startFrequency > PWLE_FREQUENCY_MAX_HZ ||
                    active.endFrequency < PWLE_FREQUENCY_MIN_HZ ||
                    active.endFrequency > PWLE_FREQUENCY_MAX_HZ) {
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
                }

                if (!((active.startAmplitude == prevEndAmplitude) &&
                      (active.startFrequency == prevEndFrequency))) {
                    if (constructActiveSegment(ch, 0, active.startAmplitude, active.startFrequency,
                                               false) < 0) {
                        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
                    }
                    incrementIndex(&segmentIdx);
                }

                if (active.startFrequency != active.endFrequency) {
                    chirp = true;
                }
                if (constructActiveSegment(ch, active.duration, active.endAmplitude,
                                           active.endFrequency, chirp) < 0) {
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
                }
                incrementIndex(&segmentIdx);

                prevEndAmplitude = active.endAmplitude;
                prevEndFrequency = active.endFrequency;
                totalDuration += active.duration;
                chirp = false;
                break;
            }
            case PrimitivePwle::braking: {
                auto braking = e.get<PrimitivePwle::braking>();
                if (braking.braking > Braking::CLAB) {
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
                } else if (!isClabSupported && (braking.braking == Braking::CLAB)) {
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
                }

                if (braking.duration > COMPOSE_PWLE_PRIMITIVE_DURATION_MAX_MS) {
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
                }

                if (constructBrakingSegment(ch, 0, braking.braking) < 0) {
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
                }
                incrementIndex(&segmentIdx);

                if (constructBrakingSegment(ch, braking.duration, braking.braking) < 0) {
                    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
                }
                incrementIndex(&segmentIdx);

                resetPreviousEndAmplitudeEndFrequency(&prevEndAmplitude, &prevEndFrequency);
                totalDuration += braking.duration;
                break;
            }
        }

        if (segmentIdx > COMPOSE_PWLE_SIZE_MAX_DEFAULT) {
            ALOGE("Too many PrimitivePwle section!");
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }
    }
    dspmem_chunk_flush(ch);

    /* Update wlength */
    totalDuration += MAX_COLD_START_LATENCY_MS;
    if (totalDuration > 0x7FFFF) {
        ALOGE("Total duration is too long (%d)!", totalDuration);
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    updateWLength(ch, totalDuration);

    /* Update nsections */
    updateNSection(ch, segmentIdx);

    return performEffect(WAVEFORM_MAX_INDEX /*ignored*/, VOLTAGE_SCALE_MAX /*ignored*/, ch,
                         callback);
}

bool Vibrator::isUnderExternalControl() {
    return mIsUnderExternalControl;
}

// BnCInterface APIs

binder_status_t Vibrator::dump(int fd, const char **args, uint32_t numArgs) {
    if (fd < 0) {
        ALOGE("Called debug() with invalid fd.");
        return STATUS_OK;
    }

    (void)args;
    (void)numArgs;

    dprintf(fd, "AIDL:\n");

    dprintf(fd, "  F0 Offset: base: %" PRIu32 " flip: %" PRIu32 "\n", mF0Offset, mF0OffsetDual);

    dprintf(fd, "  Voltage Levels:\n");
    dprintf(fd, "     Tick Effect Min: %" PRIu32 " Max: %" PRIu32 "\n", mTickEffectVol[0],
            mTickEffectVol[1]);
    dprintf(fd, "     Click Effect Min: %" PRIu32 " Max: %" PRIu32 "\n", mClickEffectVol[0],
            mClickEffectVol[1]);
    dprintf(fd, "     Long Effect Min: %" PRIu32 " Max: %" PRIu32 "\n", mLongEffectVol[0],
            mLongEffectVol[1]);

    dprintf(fd, "  FF effect:\n");
    dprintf(fd, "    Physical waveform:\n");
    dprintf(fd, "==== Base ====\n\tId\tIndex\tt   ->\tt'\ttrigger button\n");
    uint8_t effectId;
    for (effectId = 0; effectId < WAVEFORM_MAX_PHYSICAL_INDEX; effectId++) {
        dprintf(fd, "\t%d\t%d\t%d\t%d\t%X\n", mFfEffects[effectId].id,
                mFfEffects[effectId].u.periodic.custom_data[1], mEffectDurations[effectId],
                mFfEffects[effectId].replay.length, mFfEffects[effectId].trigger.button);
    }
    if (mIsDual) {
        dprintf(fd, "==== Flip ====\n\tId\tIndex\tt   ->\tt'\ttrigger button\n");
        for (effectId = 0; effectId < WAVEFORM_MAX_PHYSICAL_INDEX; effectId++) {
            dprintf(fd, "\t%d\t%d\t%d\t%d\t%X\n", mFfEffectsDual[effectId].id,
                    mFfEffectsDual[effectId].u.periodic.custom_data[1], mEffectDurations[effectId],
                    mFfEffectsDual[effectId].replay.length,
                    mFfEffectsDual[effectId].trigger.button);
        }
    }

    dprintf(fd, "Base: OWT waveform:\n");
    dprintf(fd, "\tId\tBytes\tData\tt\ttrigger button\n");
    for (effectId = WAVEFORM_MAX_PHYSICAL_INDEX; effectId < WAVEFORM_MAX_INDEX; effectId++) {
        uint32_t numBytes = mFfEffects[effectId].u.periodic.custom_len * 2;
        std::stringstream ss;
        ss << " ";
        for (int i = 0; i < numBytes; i++) {
            ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex
               << (uint16_t)(*(
                          reinterpret_cast<uint8_t *>(mFfEffects[effectId].u.periodic.custom_data) +
                          i))
               << " ";
        }
        dprintf(fd, "\t%d\t%d\t{%s}\t%u\t%X\n", mFfEffects[effectId].id, numBytes, ss.str().c_str(),
                mFfEffectsDual[effectId].replay.length, mFfEffects[effectId].trigger.button);
    }
    if (mIsDual) {
        dprintf(fd, "Flip: OWT waveform:\n");
        dprintf(fd, "\tId\tBytes\tData\tt\ttrigger button\n");
        for (effectId = WAVEFORM_MAX_PHYSICAL_INDEX; effectId < WAVEFORM_MAX_INDEX; effectId++) {
            uint32_t numBytes = mFfEffectsDual[effectId].u.periodic.custom_len * 2;
            std::stringstream ss;
            ss << " ";
            for (int i = 0; i < numBytes; i++) {
                ss << std::uppercase << std::setfill('0') << std::setw(2) << std::hex
                   << (uint16_t)(*(reinterpret_cast<uint8_t *>(
                                           mFfEffectsDual[effectId].u.periodic.custom_data) +
                                   i))
                   << " ";
            }
            dprintf(fd, "\t%d\t%d\t{%s}\t%u\t%X\n", mFfEffectsDual[effectId].id, numBytes,
                    ss.str().c_str(), mFfEffectsDual[effectId].replay.length,
                    mFfEffectsDual[effectId].trigger.button);
        }
    }
    dprintf(fd, "\n");
    dprintf(fd, "\n");

    mHwApiDef->debug(fd);

    dprintf(fd, "\n");

    mHwCalDef->debug(fd);

    if (mIsDual) {
        mHwApiDual->debug(fd);
        dprintf(fd, "\n");
        mHwCalDual->debug(fd);
    }

    fsync(fd);
    return STATUS_OK;
}

bool Vibrator::hasHapticAlsaDevice() {
    // We need to call findHapticAlsaDevice once only. Calling in the
    // constructor is too early in the boot process and the pcm file contents
    // are empty. Hence we make the call here once only right before we need to.
    if (!mConfigHapticAlsaDeviceDone) {
        if (mHwApiDef->getHapticAlsaDevice(&mCard, &mDevice)) {
            mHasHapticAlsaDevice = true;
            mConfigHapticAlsaDeviceDone = true;
        } else {
            ALOGE("Haptic ALSA device not supported");
        }
    } else {
        ALOGD("Haptic ALSA device configuration done.");
    }
    return mHasHapticAlsaDevice;
}

ndk::ScopedAStatus Vibrator::getSimpleDetails(Effect effect, EffectStrength strength,
                                              uint32_t *outEffectIndex, uint32_t *outTimeMs,
                                              uint32_t *outVolLevel) {
    uint32_t effectIndex;
    uint32_t timeMs;
    float intensity;
    uint32_t volLevel;
    switch (strength) {
        case EffectStrength::LIGHT:
            intensity = 0.5f;
            break;
        case EffectStrength::MEDIUM:
            intensity = 0.7f;
            break;
        case EffectStrength::STRONG:
            intensity = 1.0f;
            break;
        default:
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    switch (effect) {
        case Effect::TEXTURE_TICK:
            effectIndex = WAVEFORM_LIGHT_TICK_INDEX;
            intensity *= 0.5f;
            break;
        case Effect::TICK:
            effectIndex = WAVEFORM_CLICK_INDEX;
            intensity *= 0.5f;
            break;
        case Effect::CLICK:
            effectIndex = WAVEFORM_CLICK_INDEX;
            intensity *= 0.7f;
            break;
        case Effect::HEAVY_CLICK:
            effectIndex = WAVEFORM_CLICK_INDEX;
            intensity *= 1.0f;
            break;
        default:
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    volLevel = intensityToVolLevel(intensity, effectIndex);
    timeMs = mEffectDurations[effectIndex] + MAX_COLD_START_LATENCY_MS;

    *outEffectIndex = effectIndex;
    *outTimeMs = timeMs;
    *outVolLevel = volLevel;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getCompoundDetails(Effect effect, EffectStrength strength,
                                                uint32_t *outTimeMs, dspmem_chunk *outCh) {
    ndk::ScopedAStatus status;
    uint32_t timeMs = 0;
    uint32_t thisEffectIndex;
    uint32_t thisTimeMs;
    uint32_t thisVolLevel;
    switch (effect) {
        case Effect::DOUBLE_CLICK:
            dspmem_chunk_write(outCh, 8, 0); /* Padding */
            dspmem_chunk_write(outCh, 8, 2); /* nsections */
            dspmem_chunk_write(outCh, 8, 0); /* repeat */

            status = getSimpleDetails(Effect::CLICK, strength, &thisEffectIndex, &thisTimeMs,
                                      &thisVolLevel);
            if (!status.isOk()) {
                return status;
            }
            timeMs += thisTimeMs;

            dspmem_chunk_write(outCh, 8, (uint8_t)(0xFF & thisVolLevel));    /* amplitude */
            dspmem_chunk_write(outCh, 8, (uint8_t)(0xFF & thisEffectIndex)); /* index */
            dspmem_chunk_write(outCh, 8, 0);                                 /* repeat */
            dspmem_chunk_write(outCh, 8, 0);                                 /* flags */
            dspmem_chunk_write(outCh, 16,
                               (uint16_t)(0xFFFF & WAVEFORM_DOUBLE_CLICK_SILENCE_MS)); /* delay */

            timeMs += WAVEFORM_DOUBLE_CLICK_SILENCE_MS + MAX_PAUSE_TIMING_ERROR_MS;

            status = getSimpleDetails(Effect::HEAVY_CLICK, strength, &thisEffectIndex, &thisTimeMs,
                                      &thisVolLevel);
            if (!status.isOk()) {
                return status;
            }
            timeMs += thisTimeMs;

            dspmem_chunk_write(outCh, 8, (uint8_t)(0xFF & thisVolLevel));    /* amplitude */
            dspmem_chunk_write(outCh, 8, (uint8_t)(0xFF & thisEffectIndex)); /* index */
            dspmem_chunk_write(outCh, 8, 0);                                 /* repeat */
            dspmem_chunk_write(outCh, 8, 0);                                 /* flags */
            dspmem_chunk_write(outCh, 16, 0);                                /* delay */
            dspmem_chunk_flush(outCh);

            break;
        default:
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    *outTimeMs = timeMs;

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getPrimitiveDetails(CompositePrimitive primitive,
                                                 uint32_t *outEffectIndex) {
    uint32_t effectIndex;
    uint32_t primitiveBit = 1 << int32_t(primitive);
    if ((primitiveBit & mSupportedPrimitivesBits) == 0x0) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    switch (primitive) {
        case CompositePrimitive::NOOP:
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        case CompositePrimitive::CLICK:
            effectIndex = WAVEFORM_CLICK_INDEX;
            break;
        case CompositePrimitive::THUD:
            effectIndex = WAVEFORM_THUD_INDEX;
            break;
        case CompositePrimitive::SPIN:
            effectIndex = WAVEFORM_SPIN_INDEX;
            break;
        case CompositePrimitive::QUICK_RISE:
            effectIndex = WAVEFORM_QUICK_RISE_INDEX;
            break;
        case CompositePrimitive::SLOW_RISE:
            effectIndex = WAVEFORM_SLOW_RISE_INDEX;
            break;
        case CompositePrimitive::QUICK_FALL:
            effectIndex = WAVEFORM_QUICK_FALL_INDEX;
            break;
        case CompositePrimitive::LIGHT_TICK:
            effectIndex = WAVEFORM_LIGHT_TICK_INDEX;
            break;
        case CompositePrimitive::LOW_TICK:
            effectIndex = WAVEFORM_LOW_TICK_INDEX;
            break;
        default:
            return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    *outEffectIndex = effectIndex;

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::performEffect(Effect effect, EffectStrength strength,
                                           const std::shared_ptr<IVibratorCallback> &callback,
                                           int32_t *outTimeMs) {
    ndk::ScopedAStatus status;
    uint32_t effectIndex;
    uint32_t timeMs = 0;
    uint32_t volLevel;
    dspmem_chunk *ch = nullptr;
    switch (effect) {
        case Effect::TEXTURE_TICK:
            // fall-through
        case Effect::TICK:
            // fall-through
        case Effect::CLICK:
            // fall-through
        case Effect::HEAVY_CLICK:
            status = getSimpleDetails(effect, strength, &effectIndex, &timeMs, &volLevel);
            break;
        case Effect::DOUBLE_CLICK:
            ch = dspmem_chunk_create(new uint8_t[FF_CUSTOM_DATA_LEN_MAX_COMP]{0x00},
                                     FF_CUSTOM_DATA_LEN_MAX_COMP);
            status = getCompoundDetails(effect, strength, &timeMs, ch);
            volLevel = VOLTAGE_SCALE_MAX;
            break;
        default:
            status = ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
            break;
    }
    if (!status.isOk()) {
        goto exit;
    }

    status = performEffect(effectIndex, volLevel, ch, callback);

exit:
    *outTimeMs = timeMs;
    return status;
}

ndk::ScopedAStatus Vibrator::performEffect(uint32_t effectIndex, uint32_t volLevel,
                                           dspmem_chunk *ch,
                                           const std::shared_ptr<IVibratorCallback> &callback) {
    setEffectAmplitude(volLevel, VOLTAGE_SCALE_MAX);

    return on(MAX_TIME_MS, effectIndex, ch, callback);
}

void Vibrator::waitForComplete(std::shared_ptr<IVibratorCallback> &&callback) {
    ALOGD("waitForComplete: Callback status in waitForComplete(): callBack: %d",
          (callback != nullptr));

    // Bypass checking flip part's haptic state
    if (!mHwApiDef->pollVibeState(VIBE_STATE_HAPTIC, POLLING_TIMEOUT)) {
        ALOGD("Failed to get state \"Haptic\"");
    }

    mHwApiDef->pollVibeState(VIBE_STATE_STOPPED);
    // Check flip's state after base was done
    if (mIsDual) {
        mHwApiDual->pollVibeState(VIBE_STATE_STOPPED);
    }
    ALOGD("waitForComplete: get STOP");
    {
        const std::scoped_lock<std::mutex> lock(mActiveId_mutex);
        if (mActiveId >= WAVEFORM_MAX_PHYSICAL_INDEX) {
            if (!mHwApiDef->eraseOwtEffect(mInputFd, mActiveId, &mFfEffects)) {
                ALOGE("Failed to clean up the composed effect %d", mActiveId);
            }
            if (mIsDual &&
                (!mHwApiDual->eraseOwtEffect(mInputFdDual, mActiveId, &mFfEffectsDual))) {
                ALOGE("Failed to clean up flip's composed effect %d", mActiveId);
            }
        } else {
            ALOGD("waitForComplete: Vibrator is already off");
        }
        mActiveId = -1;
        if (mGPIOStatus && !mHwGPIO->setGPIOOutput(false)) {
            ALOGE("waitForComplete: Failed to reset GPIO(%d): %s", errno, strerror(errno));
        }
        // Do waveform number checking
        uint32_t effectCount = WAVEFORM_MAX_PHYSICAL_INDEX;
        mHwApiDef->getEffectCount(&effectCount);
        if (effectCount > WAVEFORM_MAX_PHYSICAL_INDEX) {
            // Forcibly clean all OWT waveforms
            if (!mHwApiDef->eraseOwtEffect(mInputFd, WAVEFORM_MAX_INDEX, &mFfEffects)) {
                ALOGE("Failed to clean up all base's composed effect");
            }
        }

        if (mIsDual) {
            // Forcibly clean all OWT waveforms
            effectCount = WAVEFORM_MAX_PHYSICAL_INDEX;
            mHwApiDual->getEffectCount(&effectCount);
            if ((effectCount > WAVEFORM_MAX_PHYSICAL_INDEX) &&
                (!mHwApiDual->eraseOwtEffect(mInputFdDual, WAVEFORM_MAX_INDEX, &mFfEffectsDual))) {
                ALOGE("Failed to clean up all flip's composed effect");
            }
        }
    }

    if (callback) {
        auto ret = callback->onComplete();
        if (!ret.isOk()) {
            ALOGE("Failed completion callback: %d", ret.getExceptionCode());
        }
    }
    ALOGD("waitForComplete: Done.");
}

uint32_t Vibrator::intensityToVolLevel(float intensity, uint32_t effectIndex) {
    uint32_t volLevel;
    auto calc = [](float intst, std::array<uint32_t, 2> v) -> uint32_t {
        return std::lround(intst * (v[1] - v[0])) + v[0];
    };

    switch (effectIndex) {
        case WAVEFORM_LIGHT_TICK_INDEX:
            volLevel = calc(intensity, mTickEffectVol);
            break;
        case WAVEFORM_QUICK_RISE_INDEX:
            // fall-through
        case WAVEFORM_QUICK_FALL_INDEX:
            volLevel = calc(intensity, mLongEffectVol);
            break;
        case WAVEFORM_CLICK_INDEX:
            // fall-through
        case WAVEFORM_THUD_INDEX:
            // fall-through
        case WAVEFORM_SPIN_INDEX:
            // fall-through
        case WAVEFORM_SLOW_RISE_INDEX:
            // fall-through
        default:
            volLevel = calc(intensity, mClickEffectVol);
            break;
    }
    return volLevel;
}

}  // namespace vibrator
}  // namespace hardware
}  // namespace android
}  // namespace aidl
