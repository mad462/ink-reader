idf_component_register(
    SRCS "app_main.c" "reader_app_model.c"
    REQUIRES ink_boot_switch ink_epd_ui ink_fonts ink_hw ink_input ink_reader_core ink_sd esp_timer)
