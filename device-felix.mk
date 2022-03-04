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

TARGET_KERNEL_DIR ?= device/google/felix-kernel
TARGET_BOARD_KERNEL_HEADERS := device/google/felix-kernel/kernel-headers
TARGET_RECOVERY_DEFAULT_ROTATION := ROTATION_RIGHT

$(call inherit-product-if-exists, vendor/google_devices/felix/prebuilts/device-vendor-felix.mk)
$(call inherit-product-if-exists, vendor/google_devices/gs201/prebuilts/device-vendor.mk)
$(call inherit-product-if-exists, vendor/google_devices/gs201/proprietary/device-vendor.mk)
$(call inherit-product-if-exists, vendor/google_devices/felix/proprietary/felix/device-vendor-felix.mk)

DEVICE_PACKAGE_OVERLAYS += device/google/felix/felix/overlay

include device/google/gs201/device-shipping-common.mk
include device/google/felix/audio/felix/audio-tables.mk
include hardware/google/pixel/vibrator/cs40l26/device-stereo.mk
include device/google/gs101/bluetooth/bluetooth.mk
ifeq ($(filter factory_felix, $(TARGET_PRODUCT)),)
include device/google/gs101/uwb/uwb.mk
include device/google/felix/uwb/uwb_calibration.mk
endif

$(call soong_config_set,lyric,tuning_product,felix)
$(call soong_config_set,google3a_config,target_device,felix)

BOARD_SEPOLICY_DIRS += \
    hardware/google/pixel-sepolicy/vibrator/common \

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

# Display Config
PRODUCT_COPY_FILES += \
	device/google/felix/felix/display_colordata_cal1.pb:$(TARGET_COPY_OUT_VENDOR)/etc/display_colordata_cal1.pb \
	device/google/felix/felix/display_colordata_dev_cal1.pb:$(TARGET_COPY_OUT_VENDOR)/etc/display_colordata_dev_cal1.pb \
	device/google/felix/felix/display_golden_cal0.pb:$(TARGET_COPY_OUT_VENDOR)/etc/display_golden_cal0.pb \
	device/google/felix/felix/display_golden_cal1.pb:$(TARGET_COPY_OUT_VENDOR)/etc/display_golden_cal1.pb

# NFC
PRODUCT_COPY_FILES += \
	frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
	frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hce.xml \
	frameworks/native/data/etc/android.hardware.nfc.hcef.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hcef.xml \
	frameworks/native/data/etc/com.nxp.mifare.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.nxp.mifare.xml \
	frameworks/native/data/etc/android.hardware.nfc.uicc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.uicc.xml \
	frameworks/native/data/etc/android.hardware.nfc.ese.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.ese.xml \
	device/google/felix/nfc/libnfc-hal-st.conf:$(TARGET_COPY_OUT_VENDOR)/etc/libnfc-hal-st.conf \
	device/google/felix/nfc/libnfc-nci-felix.conf:$(TARGET_COPY_OUT_PRODUCT)/etc/libnfc-nci.conf

PRODUCT_PACKAGES += \
	NfcNci \
	Tag \
	android.hardware.nfc@1.2-service.st

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
	device/google/felix/nfc/manifest_nfc.xml \
	device/google/felix/nfc/manifest_se.xml

# Thermal Config
PRODUCT_COPY_FILES += \
	device/google/felix/thermal_info_config_felix.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal_info_config.json \
	device/google/felix/thermal_info_config_felix_proto.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal_info_config_proto.json

# Power HAL config
PRODUCT_COPY_FILES += \
	device/google/felix/powerhint.json:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.json

# Bluetooth HAL
DEVICE_MANIFEST_FILE += \
	device/google/pantah/bluetooth/manifest_bluetooth.xml
PRODUCT_SOONG_NAMESPACES += \
        vendor/broadcom/bluetooth
PRODUCT_PACKAGES += \
	android.hardware.bluetooth@1.1-service.bcmbtlinux \
	bt_vendor.conf
PRODUCT_COPY_FILES += \
	device/google/pantah/bluetooth/bt_vendor_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth/bt_vendor_overlay.conf

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

# default BDADDR for EVB only
PRODUCT_PROPERTY_OVERRIDES += \
	ro.vendor.bluetooth.evb_bdaddr="22:22:22:33:44:55"

# PowerStats HAL
PRODUCT_SOONG_NAMESPACES += \
    device/google/felix/powerstats/felix

# Remove when b/182979111 is resolved.
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += ro.surface_flinger.primary_display_orientation=ORIENTATION_90

# Increment the SVN for any official public releases
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.build.svn=1

# Vibrator HAL
PRODUCT_PRODUCT_PROPERTIES +=\
    ro.vendor.vibrator.hal.long.frequency.shift=0
ACTUATOR_MODEL := luxshare_ict_lt_xlra1906d

# DCK properties based on target
PRODUCT_PROPERTY_OVERRIDES += \
    ro.gms.dck.eligible_wcc=3

# Bluetooth SAR test tool
PRODUCT_PACKAGES_DEBUG += \
    sar_test

# WirelessCharger
DEVICE_PRODUCT_COMPATIBILITY_MATRIX_FILE += device/google/gs101/device_framework_matrix_product_wireless.xml

# Bluetooth
PRODUCT_PRODUCT_PROPERTIES += \
    persist.bluetooth.a2dp_aac.vbr_supported=true

# Bluetooth HAL
PRODUCT_PACKAGES += \
	bt_vendor.conf

# Graphics
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += ro.surface_flinger.enable_frame_rate_override=true
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += debug.sf.set_idle_timer_ms_4619827677550801152=80
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += debug.sf.support_kernel_idle_timer_4619827677550801152=true
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += debug.sf.set_idle_timer_ms_4619827677550801153=1500
PRODUCT_DEFAULT_PROPERTY_OVERRIDES += debug.sf.support_kernel_idle_timer_4619827677550801153=false

# Voice packs for Text-To-Speech
PRODUCT_COPY_FILES += \
	device/google/felix/tts/ja-jp/ja-jp-x-multi-darwinn-wavernn-r27.zvoice:product/tts/google/ja-jp/ja-jp-x-multi-darwinn-wavernn-r27.zvoice\
	device/google/felix/tts/ja-jp/ja-jp-x-multi-r27.zvoice:product/tts/google/ja-jp/ja-jp-x-multi-r27.zvoice\
	device/google/felix/tts/ja-jp/ja-jp-x-multi-wavernn-r27.zvoice:product/tts/google/ja-jp/ja-jp-x-multi-wavernn-r27.zvoice\
	device/google/felix/tts/fr-fr/fr-fr-x-multi-darwinn-wavernn-r27.zvoice:product/tts/google/fr-fr/fr-fr-x-multi-darwinn-wavernn-r27.zvoice\
	device/google/felix/tts/fr-fr/fr-fr-x-multi-r27.zvoice:product/tts/google/fr-fr/fr-fr-x-multi-r27.zvoice\
	device/google/felix/tts/fr-fr/fr-fr-x-multi-wavernn-r27.zvoice:product/tts/google/fr-fr/fr-fr-x-multi-wavernn-r27.zvoice\
	device/google/felix/tts/de-de/de-de-x-multi-darwinn-wavernn-r27.zvoice:product/tts/google/de-de/de-de-x-multi-darwinn-wavernn-r27.zvoice\
	device/google/felix/tts/de-de/de-de-x-multi-r27.zvoice:product/tts/google/de-de/de-de-x-multi-r27.zvoice\
	device/google/felix/tts/de-de/de-de-x-multi-wavernn-r27.zvoice:product/tts/google/de-de/de-de-x-multi-wavernn-r27.zvoice\
	device/google/felix/tts/it-it/it-it-x-multi-r24.zvoice:product/tts/google/it-it/it-it-x-multi-r24.zvoice\
	device/google/felix/tts/es-es/es-es-x-multi-darwinn-wavernn-r27.zvoice:product/tts/google/es-es/es-es-x-multi-darwinn-wavernn-r27.zvoice\
	device/google/felix/tts/es-es/es-es-x-multi-r27.zvoice:product/tts/google/es-es/es-es-x-multi-r27.zvoice\
	device/google/felix/tts/es-es/es-es-x-multi-wavernn-r27.zvoice:product/tts/google/es-es/es-es-x-multi-wavernn-r27.zvoice

# Set zram size
PRODUCT_VENDOR_PROPERTIES += \
    vendor.zram.size=3g

# SKU specific RROs
PRODUCT_PACKAGES += \
    SettingsOverlayGPQ72

# Trusty liboemcrypto.so
PRODUCT_SOONG_NAMESPACES += vendor/google_devices/felix/prebuilts

# Set zram size
PRODUCT_VENDOR_PROPERTIES += \
       vendor.zram.size=3g
