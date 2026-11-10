#
# What the Echo Show 8 (2nd gen) gets on top of its own device tree in
# sources/device/amazon/athena, which inherits this file. stage-tree.sh
# copies device/athena/ to device/kiosk-satellite/athena in the tree.
#

# The network name EchoService gives the Show: echo-show-8-g2-ks and the end
# of its serial.
PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.echo.hostname=echo-show-8-g2-ks
