#
# What the Echo Show 8 (2nd gen) gets on top of its own device tree in
# sources/device/amazon/athena, which inherits this file. stage-tree.sh
# copies device/athena/ to device/kiosk-satellite/athena in the tree.
#

# The network name EchoService gives the Show: echo-show-8-g2-ks and the end
# of its serial.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.hostname=echo-show-8-g2-ks

# A fresh install starts at master volume 18 of 30, which Kiosk Satellite
# shows as 60%. Fire OS's vendor partition gives every stream 30 steps. The
# guide recommends 60% with Kiosk Satellite's media player, intercom and
# assistant volumes at 100%.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.media_vol_default=18
