#
# Copyright (C) 2021 The Android Open-Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Restrict the visibility of Android.bp files to improve build analysis time
$(call inherit-product-if-exists, vendor/google/products/sources_pixel.mk)

TARGET_KERNEL_DIR ?= device/google/felix-kernel
TARGET_BOARD_KERNEL_HEADERS := device/google/felix-kernel/kernel-headers
TARGET_RECOVERY_DEFAULT_ROTATION := ROTATION_RIGHT

$(call inherit-product-if-exists, vendor/google_devices/felix/prebuilts/device-vendor-felix.mk)
$(call inherit-product-if-exists, vendor/google_devices/gs201/prebuilts/device-vendor.mk)
$(call inherit-product-if-exists, vendor/google_devices/gs201/proprietary/device-vendor.mk)
$(call inherit-product-if-exists, vendor/google_devices/felix/proprietary/felix/device-vendor-felix.mk)
$(call inherit-product-if-exists, vendor/google_devices/felix/proprietary/device-vendor.mk)
$(call inherit-product-if-exists, vendor/google_devices/felix/proprietary/WallpapersFelix.mk)

$(call inherit-product, device/google/felix/uwb/uwb_calibration_country.mk)

DEVICE_PACKAGE_OVERLAYS += device/google/felix/felix/overlay

include device/google/felix/audio/felix/audio-tables.mk
include device/google/gs201/device-shipping-common.mk
$(call soong_config_set,fp_hal_feature,pixel_product, product_a)
include device/google/felix/vibrator/cs40l26/device.mk
include device/google/gs-common/bcmbt/bluetooth.mk
include device/google/gs-common/display/dump_second_display.mk
include device/google/gs-common/touch/gti/gti.mk
include device/google/gs-common/touch/stm/stm6.mk
ifeq ($(filter factory_felix, $(TARGET_PRODUCT)),)
include device/google/felix/uwb/uwb_calibration.mk
endif

# go/lyric-soong-variables
$(call soong_config_set,lyric,camera_hardware,felix)
$(call soong_config_set,lyric,tuning_product,felix)
$(call soong_config_set,google3a_config,target_device,felix)

# Init files
PRODUCT_COPY_FILES += \
	device/google/felix/conf/init.felix.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.felix.rc

# Recovery files
PRODUCT_COPY_FILES += \
	device/google/felix/conf/init.recovery.device.rc:$(TARGET_COPY_OUT_RECOVERY)/root/init.recovery.felix.rc

# insmod files
PRODUCT_COPY_FILES += \
	device/google/felix/init.insmod.felix.cfg:$(TARGET_COPY_OUT_VENDOR)/etc/init.insmod.felix.cfg

# Camera
PRODUCT_COPY_FILES += \
	device/google/felix/media_profiles_felix.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml

# Media Performance Class 13
PRODUCT_PROPERTY_OVERRIDES += ro.odm.build.media_performance_class=33

# sysconfig and permissions XML from stock
PRODUCT_COPY_FILES += \
	$(LOCAL_PATH)/product-sysconfig-stock.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/sysconfig/product-sysconfig-stock.xml \
    $(LOCAL_PATH)/product-permissions-stock.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/permissions/product-permissions-stock.xml

# Display Config
PRODUCT_COPY_FILES += \
	device/google/felix/felix/display_colordata_cal1.pb:$(TARGET_COPY_OUT_VENDOR)/etc/display_colordata_cal1.pb \
	device/google/felix/felix/display_colordata_dev_cal1.pb:$(TARGET_COPY_OUT_VENDOR)/etc/display_colordata_dev_cal1.pb \
	device/google/felix/felix/display_golden_cal0.pb:$(TARGET_COPY_OUT_VENDOR)/etc/display_golden_cal0.pb \
	device/google/felix/felix/display_golden_cal1.pb:$(TARGET_COPY_OUT_VENDOR)/etc/display_golden_cal1.pb

# Display
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
	vendor.display.lbe.supported=1 \
	vendor.display.async_off.supported=true \
	ro.surface_flinger.ignore_hdr_camera_layers=true

#config of display brightness dimming
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += vendor.display.0.brightness.dimming.usage=1
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += vendor.display.1.brightness.dimming.usage=2

# Early wake up sysfs path for the secondary display
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += \
	vendor.display.secondary_early_wakeup_node=/sys/devices/platform/1c241000.drmdecon/early_wakeup

# NFC
PRODUCT_COPY_FILES += \
	frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
	frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hce.xml \
	frameworks/native/data/etc/android.hardware.nfc.hcef.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hcef.xml \
	frameworks/native/data/etc/com.nxp.mifare.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.nxp.mifare.xml \
	frameworks/native/data/etc/android.hardware.nfc.ese.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.ese.xml \
	device/google/felix/nfc/libnfc-hal-st.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-hal-st.conf \
	device/google/felix/nfc/libnfc-nci-felix.conf:$(TARGET_COPY_OUT_PRODUCT)/etc/libnfc-nci.conf

PRODUCT_PACKAGES += \
	$(RELEASE_PACKAGE_NFC_STACK) \
	Tag \
	android.hardware.nfc-service.st \
	NfcOverlayFelix

# SecureElement
PRODUCT_PACKAGES += \
	android.hardware.secure_element@1.2-service-gto \
	android.hardware.secure_element@1.2-service-gto-ese2

PRODUCT_COPY_FILES += \
	frameworks/native/data/etc/android.hardware.se.omapi.ese.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.se.omapi.ese.xml \
	frameworks/native/data/etc/android.hardware.se.omapi.uicc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.se.omapi.uicc.xml \
	device/google/felix/nfc/libse-gto-hal.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libse-gto-hal.conf \
	device/google/felix/nfc/libse-gto-hal2.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libse-gto-hal2.conf

DEVICE_MANIFEST_FILE += \
	device/google/felix/nfc/manifest_se.xml

# Thermal Config
PRODUCT_COPY_FILES += \
	device/google/felix/thermal_info_config_felix.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal_info_config.json \
	device/google/felix/thermal_info_config_proactive_skin_felix.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal_info_config_proactive_skin.json \
	device/google/felix/thermal_info_config_charge_felix.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal_info_config_charge.json

# Power HAL config
PRODUCT_COPY_FILES += \
	device/google/felix/powerhint.json:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.json

# Bluetooth HAL
PRODUCT_COPY_FILES += \
	device/google/felix/bluetooth/bt_vendor_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth/bt_vendor_overlay.conf
PRODUCT_PROPERTY_OVERRIDES += \
    ro.bluetooth.a2dp_offload.supported=true \
    persist.bluetooth.a2dp_offload.disabled=false \
    persist.bluetooth.a2dp_offload.cap=sbc-aac-aptx-aptxhd-ldac-opus

# Bluetooth Tx power caps
PRODUCT_COPY_FILES += \
    device/google/felix/bluetooth/bluetooth_power_limits_felix_US.csv:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_power_limits.csv \
    device/google/felix/bluetooth/bluetooth_power_limits_felix_JP.csv:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_power_limits_JP.csv \
    device/google/felix/bluetooth/bluetooth_power_limits_felix_EU.csv:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_power_limits_EU.csv \
    device/google/felix/bluetooth/bluetooth_power_limits_felix_US.csv:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_power_limits_US.csv

# Spatial Audio
PRODUCT_PACKAGES += \
	libspatialaudio

# optimize spatializer effect
PRODUCT_PROPERTY_OVERRIDES += \
       audio.spatializer.effect.util_clamp_min=300

# declare use of spatial audio
PRODUCT_PROPERTY_OVERRIDES += \
       ro.audio.spatializer_enabled=true \
       ro.audio.spatializer_transaural_enabled_default=false \
       persist.vendor.audio.spatializer.speaker_enabled=true

# Bluetooth SAR test tool
PRODUCT_PACKAGES_DEBUG += \
    sar_test

# Bluetooth hci_inject test tool
PRODUCT_PACKAGES_DEBUG += \
    hci_inject

# Bluetooth
PRODUCT_PRODUCT_PROPERTIES += \
    persist.bluetooth.a2dp_aac.vbr_supported=true

# default BDADDR for EVB only
PRODUCT_PROPERTY_OVERRIDES += \
	ro.vendor.bluetooth.evb_bdaddr="22:22:22:33:44:55"

# Keymaster HAL
#LOCAL_KEYMASTER_PRODUCT_PACKAGE ?= android.hardware.keymaster@4.1-service

# Gatekeeper HAL
#LOCAL_GATEKEEPER_PRODUCT_PACKAGE ?= android.hardware.gatekeeper@1.0-service.software


# Gatekeeper
# PRODUCT_PACKAGES += \
# 	android.hardware.gatekeeper@1.0-service.software

# Keymint replaces Keymaster
# PRODUCT_PACKAGES += \
# 	android.hardware.security.keymint-service

# Keymaster
#PRODUCT_PACKAGES += \
#	android.hardware.keymaster@4.0-impl \
#	android.hardware.keymaster@4.0-service

#PRODUCT_PACKAGES += android.hardware.keymaster@4.0-service.remote
#PRODUCT_PACKAGES += android.hardware.keymaster@4.1-service.remote
#LOCAL_KEYMASTER_PRODUCT_PACKAGE := android.hardware.keymaster@4.1-service
#LOCAL_KEYMASTER_PRODUCT_PACKAGE ?= android.hardware.keymaster@4.1-service

# PRODUCT_PROPERTY_OVERRIDES += \
# 	ro.hardware.keystore_desede=true \
# 	ro.hardware.keystore=software \
# 	ro.hardware.gatekeeper=software

# PowerStats HAL
PRODUCT_SOONG_NAMESPACES += \
    device/google/felix/powerstats/felix \
    device/google/felix

# Increment the SVN for any official public releases
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.build.svn=42

# Vibrator HAL
PRODUCT_VENDOR_PROPERTIES +=\
    ro.vendor.vibrator.hal.long.frequency.shift=0 \
    ro.vendor.vibrator.hal.gpio.num=44 \
    ro.vendor.vibrator.hal.gpio.shift=2 \
    persist.vendor.vibrator.hal.chirp.enabled=0
ACTUATOR_MODEL := luxshare_ict_lt_xlra1906d

# Fingerprint
include device/google/gs101/fingerprint/fpc1540/sw42/fpc1540.mk
FPC_MODULE_TYPE=1542_C
# Fingerprint config
include device/google/felix/fingerprint_config.mk

# The default value of this variable is false and should only be set to true when
# the device allows users to enable the seamless transfer feature.
PRODUCT_PRODUCT_PROPERTIES += \
   euicc.seamless_transfer_enabled_in_non_qs=true

# DCK properties based on target
PRODUCT_PROPERTY_OVERRIDES += \
    ro.gms.dck.eligible_wcc=3 \
    ro.gms.dck.se_capability=1

# Graphics
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += ro.surface_flinger.enable_frame_rate_override=true
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += debug.sf.set_idle_timer_ms_4619827677550801152=80
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += debug.sf.support_kernel_idle_timer_4619827677550801152=true
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += debug.sf.set_idle_timer_ms_4619827677550801153=1000
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += debug.sf.support_kernel_idle_timer_4619827677550801153=false

# Set zram size
PRODUCT_VENDOR_PROPERTIES += \
    vendor.zram.size=3g

# SKU specific RROs
PRODUCT_PACKAGES += \
    SettingsOverlayG0B96 \
    SettingsOverlayG9FPL

# Trusty liboemcrypto.so
PRODUCT_SOONG_NAMESPACES += vendor/google_devices/felix/prebuilts
ifneq (,$(filter AP1%,$(RELEASE_PLATFORM_VERSION)))
PRODUCT_SOONG_NAMESPACES += vendor/google_devices/felix/prebuilts/trusty/24Q1
else ifneq (,$(filter AP2%,$(RELEASE_PLATFORM_VERSION)))
PRODUCT_SOONG_NAMESPACES += vendor/google_devices/felix/prebuilts/trusty/24Q2
else
PRODUCT_SOONG_NAMESPACES += vendor/google_devices/felix/prebuilts/trusty/trunk
endif

# Set zram size
PRODUCT_VENDOR_PROPERTIES += \
       vendor.zram.size=3g

# Set support one-handed mode
PRODUCT_PRODUCT_PROPERTIES += \
    ro.support_one_handed_mode=false

# Hinge angle sensor
PRODUCT_COPY_FILES += \
	frameworks/native/data/etc/android.hardware.sensor.hinge_angle.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.hinge_angle.xml

# Location
ifneq (,$(filter userdebug eng, $(TARGET_BUILD_VARIANT)))
    PRODUCT_COPY_FILES += \
        device/google/felix/location/gps.xml.f10:$(TARGET_COPY_OUT_VENDOR)/etc/gnss/gps.xml \
        device/google/felix/location/lhd.conf.f10:$(TARGET_COPY_OUT_VENDOR)/etc/gnss/lhd.conf \
        device/google/felix/location/scd.conf.f10:$(TARGET_COPY_OUT_VENDOR)/etc/gnss/scd.conf
else
    PRODUCT_COPY_FILES += \
        device/google/felix/location/gps_user.xml.f10:$(TARGET_COPY_OUT_VENDOR)/etc/gnss/gps.xml \
        device/google/felix/location/lhd_user.conf.f10:$(TARGET_COPY_OUT_VENDOR)/etc/gnss/lhd.conf \
        device/google/felix/location/scd_user.conf.f10:$(TARGET_COPY_OUT_VENDOR)/etc/gnss/scd.conf
endif

PRODUCT_PACKAGES += \
        UwbOverlayF10 \
        WifiOverlay2023Mid_F10

# MIPI Coex Configs
PRODUCT_COPY_FILES += \
    device/google/felix/felix/radio/felix_camera_front_inner_mipi_coex_table.csv:$(TARGET_COPY_OUT_VENDOR)/etc/modem/camera_front_inner_mipi_coex_table.csv \
    device/google/felix/felix/radio/felix_display_secondary_mipi_coex_table.csv:$(TARGET_COPY_OUT_VENDOR)/etc/modem/display_secondary_mipi_coex_table.csv

PRODUCT_SOONG_NAMESPACES += device/google/felix

DEVICE_PRODUCT_COMPATIBILITY_MATRIX_FILE += device/google/felix/device_framework_matrix_product_felix.xml

# Device features
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/handheld_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml

# Increase thread priority for nodes stop
PRODUCT_VENDOR_PROPERTIES += \
    persist.vendor.camera.increase_thread_priority_nodes_stop=true

##Audio Vendor property
PRODUCT_PROPERTY_OVERRIDES += \
	persist.vendor.audio.cca.enabled=false

# Camera
PRODUCT_PROPERTY_OVERRIDES += \
    persist.vendor.camera.adjust_backend_min_freq_for_1p_front_video_1080p_30fps=1 \
    persist.vendor.camera.extended_launch_boost=1 \
    persist.vendor.camera.multicam_streaming_boost=1 \
    persist.vendor.camera.optimized_tnr_freq=1 \
    persist.vendor.camera.raise_buf_allocation_priority=1 \
    persist.vendor.camera.start_cpu_throttling_at_moderate_thermal=1 \
    camera.enable_landscape_to_portrait=true \
    persist.vendor.camera.debug.bypass_csi_link_error=true \
    vendor.camera.allow_sensor_binning_aspect_ratio_to_override_itp_output=false \
    vendor.camera.support_specific_stream_aspect_ratio=0.75

# Enable camera exif model/make reporting
PRODUCT_VENDOR_PROPERTIES += \
    persist.vendor.camera.exif_reveal_make_model=true

# Enable front camera always binning for 720P or smaller resolution
PRODUCT_VENDOR_PROPERTIES += \
    persist.vendor.camera.front_720P_always_binning=true

# Bluetooth OPUS codec
PRODUCT_PRODUCT_PROPERTIES += \
    persist.bluetooth.opus.enabled=true

# WLC userdebug specific
ifneq (,$(filter userdebug eng, $(TARGET_BUILD_VARIANT)))
    PRODUCT_COPY_FILES += \
        device/google/gs201/init.hardware.wlc.rc.userdebug:$(TARGET_COPY_OUT_VENDOR)/etc/init/init.wlc.rc
endif

# Bluetooth LE Audio
PRODUCT_PRODUCT_PROPERTIES += \
    ro.bluetooth.leaudio_offload.supported=true \
    persist.bluetooth.leaudio_offload.disabled=false \
    ro.bluetooth.leaudio_switcher.supported=true \
    bluetooth.profile.bap.unicast.client.enabled=true \
    bluetooth.profile.csip.set_coordinator.enabled=true \
    bluetooth.profile.hap.client.enabled=true \
    bluetooth.profile.mcp.server.enabled=true \
    bluetooth.profile.ccp.server.enabled=true \
    bluetooth.profile.vcp.controller.enabled=true \

# Override BQR mask to enable LE Audio Choppy report
ifneq (,$(filter userdebug eng, $(TARGET_BUILD_VARIANT)))
PRODUCT_PRODUCT_PROPERTIES += \
    persist.bluetooth.bqr.event_mask=262238
else
PRODUCT_PRODUCT_PROPERTIES += \
    persist.bluetooth.bqr.event_mask=94
endif

# Bluetooth LE Audio CIS handover to SCO
# Set the property only if the controller doesn't support CIS and SCO
# simultaneously. More details in b/242908683.
PRODUCT_PRODUCT_PROPERTIES += \
    persist.bluetooth.leaudio.notify.idle.during.call=true

# BT controller not able to support LE Audio dual mic SWB call
PRODUCT_PRODUCT_PROPERTIES += \
    bluetooth.leaudio.dual_bidirection_swb.supported=false

# LE Audio Offload Capabilities Setting
PRODUCT_COPY_FILES += \
    device/google/felix/bluetooth/le_audio_codec_capabilities.xml:$(TARGET_COPY_OUT_VENDOR)/etc/le_audio_codec_capabilities.xml

# LE Audio Unicast Allowlist
PRODUCT_PRODUCT_PROPERTIES += \
    persist.bluetooth.leaudio.allow_list=SM-R510

# Bluetooth EWP test tool
PRODUCT_PACKAGES_DEBUG += \
    ewp_tool

# Enable DeviceAsWebcam support
PRODUCT_VENDOR_PROPERTIES += \
    ro.usb.uvc.enabled=true

# Quick Start device-specific settings
PRODUCT_PRODUCT_PROPERTIES += \
    ro.quick_start.oem_id=00e0 \
    ro.quick_start.device_id=felix
