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

TARGET_RECOVERY_DEFAULT_ROTATION := ROTATION_RIGHT

TARGET_LINUX_KERNEL_VERSION := $(RELEASE_KERNEL_FELIX_VERSION)
# Keeps flexibility for kasan and ufs builds
TARGET_KERNEL_DIR ?= $(RELEASE_KERNEL_FELIX_DIR)
TARGET_BOARD_KERNEL_HEADERS ?= $(RELEASE_KERNEL_FELIX_DIR)/kernel-headers

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
include device/google/gs-common/touch/gti/predump_gti_dual.mk
include device/google/gs-common/touch/stm/predump_stm6.mk
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

# TODO: Verify this is not needed for current 6.1 release
# insmod files. Kernel 5.10 prebuilts don't provide these yet, so provide our
# own copy if they're not in the prebuilts.
# TODO(b/369686096): drop this when 5.10 is gone.
ifeq ($(wildcard $(TARGET_KERNEL_DIR)/init.insmod.*.cfg),)
PRODUCT_COPY_FILES += \
	device/google/felix/init.insmod.felix.cfg:$(TARGET_COPY_OUT_VENDOR_DLKM)/etc/init.insmod.felix.cfg
endif

# NFC
PRODUCT_COPY_FILES += \
	frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
	frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hce.xml \
	frameworks/native/data/etc/android.hardware.nfc.hcef.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hcef.xml \
	frameworks/native/data/etc/com.nxp.mifare.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.nxp.mifare.xml \
	frameworks/native/data/etc/android.hardware.nfc.ese.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.ese.xml \

PRODUCT_PACKAGES += \
	$(RELEASE_PACKAGE_NFC_STACK) \
	Tag \
	android.hardware.nfc-service.st \
	NfcOverlayFelix

# Shared Modem Platform
SHARED_MODEM_PLATFORM_VENDOR := lassen

# Shared Modem Platform
include device/google/gs-common/modem/modem_svc_sit/shared_modem_platform.mk

# SecureElement
PRODUCT_PACKAGES += \
	android.hardware.secure_element@1.2-service-gto \
	android.hardware.secure_element@1.2-service-gto-ese2

PRODUCT_COPY_FILES += \
	frameworks/native/data/etc/android.hardware.se.omapi.ese.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.se.omapi.ese.xml \
	frameworks/native/data/etc/android.hardware.se.omapi.uicc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.se.omapi.uicc.xml \

# Spatial Audio
PRODUCT_PACKAGES += \
	libspatialaudio

# Bluetooth SAR test tool
PRODUCT_PACKAGES_DEBUG += \
    sar_test

# Bluetooth hci_inject test tool
PRODUCT_PACKAGES_DEBUG += \
    hci_inject

# PowerStats HAL
PRODUCT_SOONG_NAMESPACES += \
    device/google/felix

# Increment the SVN for any official public releases
ifdef RELEASE_SVN_FELIX
TARGET_SVN ?= $(RELEASE_SVN_FELIX)
else
# Set this for older releases that don't use build flag
TARGET_SVN ?= 55
endif

PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.build.svn=$(TARGET_SVN)

# Set device family property for SMR
PRODUCT_PROPERTY_OVERRIDES += \
    ro.build.device_family=F10

# Set build properties for SMR builds
ifeq ($(RELEASE_IS_SMR), true)
    ifneq (,$(RELEASE_BASE_OS_FELIX))
        PRODUCT_BASE_OS := $(RELEASE_BASE_OS_FELIX)
    endif
endif

# Set build properties for EMR builds
ifeq ($(RELEASE_IS_EMR), true)
    ifneq (,$(RELEASE_BASE_OS_FELIX))
        PRODUCT_PROPERTY_OVERRIDES += \
        ro.build.version.emergency_base_os=$(RELEASE_BASE_OS_FELIX)
    endif
endif

ACTUATOR_MODEL := luxshare_ict_lt_xlra1906d

# Fingerprint
include device/google/gs101/fingerprint/fpc1540/sw42/fpc1540.mk
FPC_MODULE_TYPE=1542_C
# Fingerprint config
include device/google/felix/fingerprint_config.mk

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
PRODUCT_COPY_FILES += \
    device/google/felix/location/gps_user.6.1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/gnss/gps.xml

PRODUCT_PACKAGES += \
        UwbOverlayF10 \
        WifiOverlay2023Mid_F10

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

# LE Audio Unicast Allowlist
PRODUCT_PRODUCT_PROPERTIES += \
    persist.bluetooth.leaudio.allow_list=SM-R510,WF-1000XM5,SM-R630

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

# Set support hide display cutout feature
PRODUCT_PRODUCT_PROPERTIES += \
    ro.support_hide_display_cutout=true

PRODUCT_PACKAGES += \
    NoCutoutOverlay \
    AvoidAppsInCutoutOverlay

# Bluetooth device id
# Felix: 0x410C
PRODUCT_PRODUCT_PROPERTIES += \
    bluetooth.device_id.product_id=16652
