idf_component_register(
    SRCS "ink_usb_msc_core.c" "ink_usb_msc_state.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_driver_sdmmc sdmmc esp_tinyusb)
