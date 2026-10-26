#
# What the Echo Show 8 (1st gen) gets on top of the amazon-oss crown tree.
# stage-tree.sh copies device/crown/ to device/kiosk-satellite/crown in the
# tree.
#

# The network name EchoService gives the Show: echo-show-8-ks and the end of
# its serial.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.hostname=echo-show-8-ks

# Echo cancellation on the microphones (common/audio/amznaec), with the
# Echo Show 5's engine and gain to start from. The Show 8's FPGA sends four
# microphones and the copy of the speaker signal in a six channel stream.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.vendor.amznaec.engine=2 \
    ro.vendor.amznaec.gain_db=20

# Media volume 10 of 15 on a fresh install, which Kiosk Satellite shows as
# 70%, as on the Echo Show 5.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.media_vol_default=10

# The Show 8's privacy chip turns privacy on and off at the release of a hold
# of about a second and a half. Any press turns it off. The Echo Show 5's chip
# acts on a short press instead. So a press released before half a second turns the
# screen on and off, Android does nothing on a longer one and leaves a press
# to the chip while privacy is on (LONG_PRESS_POWER_PRIVACY_HOLD,
# patches/crown/frameworks/base, set by EchoService).
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.privacy_hold=1 \
    ro.echo.button_long_press=8 \
    ro.config.power_privacy_state=/sys/devices/platform/amazon-gating/state
