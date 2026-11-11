#
# The Kiosk Satellite Edition layer every Echo inherits, the Echo Dot
# included. What only the Echo Shows get is in show.mk. stage-tree.sh copies
# common/ to vendor/kiosk-satellite in the tree.
#

# WPA2/WPA3 transition mode networks join with WPA2 (rro/Wifi).
# The overlay goes on the vendor partition. An Echo whose vendor image the
# build cannot add to sets ECHO_WIFI_OVERLAY_IN_PRODUCT to true before it
# inherits this file and gets it on the product partition.
ifeq ($(ECHO_WIFI_OVERLAY_IN_PRODUCT),true)
PRODUCT_PACKAGES += \
    EchoWifiOverlayProduct
else
PRODUCT_PACKAGES += \
    EchoWifiOverlay
endif

# Amazon's wake word engine from files the user puts in /data/pryon (pryond/).
# The image ships none of Amazon's files: pryond only reports to Kiosk
# Satellite what it finds there and listens with it. The property tells
# Kiosk Satellite the service is there.
PRODUCT_PACKAGES += \
    pryond \
    pryond-tvm-link

PRODUCT_SYSTEM_DEFAULT_PROPERTIES += \
    ro.kiosk_satellite.pryon=1
