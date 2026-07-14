idf_component_register(
    SRCS "app_main.c" "reader_app_model.c" "reader_hold_paging.c" "reader_input_queue.c" "reader_page_cache.c" "reader_page_turn.c" "reader_refresh_policy.c"
    REQUIRES ink_boot_switch ink_epd_ui ink_fonts ink_hw ink_input ink_reader_core ink_sd esp_timer)
