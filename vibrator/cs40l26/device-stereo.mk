PRODUCT_PACKAGES += \
    android.hardware.vibrator-service.cs40l26-private \
    android.hardware.vibrator-service.cs40l26-dual-private \
    android.hardware.vibrator-service.cs40l26-stereo-private \

BOARD_SEPOLICY_DIRS += \
    device/google/felix-sepolicy/vibrator/common \
    device/google/felix-sepolicy/vibrator/cs40l26
