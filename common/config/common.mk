#
# The Kiosk Satellite Edition layer every Echo inherits, the Echo Dot
# included. What only the Echo Shows get is in show.mk. stage-tree.sh copies
# common/ to vendor/kiosk-satellite in the tree.
#

# WPA2/WPA3 transition mode networks join with WPA2 (rro/Wifi).
PRODUCT_PACKAGES += \
    EchoWifiOverlay
