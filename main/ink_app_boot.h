#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/sdmmc_host.h"
#include "esp_err.h"

#include "ink_app_priv.h"

esp_err_t ink_app_persist_state(const ink_app_state_t *state);
esp_err_t ink_app_ensure_photo_directory(void);
bool ink_app_open_book_from_path(ink_ui_model_t *model, const char *path);
bool ink_app_open_fixed_sample_book(ink_ui_model_t *model);
bool ink_app_load_reader_font(ink_cpfont_t *font);
bool ink_app_load_footer_font(ink_cpfont_t *font);
bool ink_app_load_menu_font(ink_cpfont_t *font);
uint8_t *ink_app_alloc_display_buffer(const char *name, size_t length);
esp_err_t ink_app_mount_tf_card(void);
esp_err_t ink_app_unmount_tf_card(void);
esp_err_t ink_app_open_tf_card_for_usb(sdmmc_card_t **out_card);
void ink_app_close_tf_card_for_usb(sdmmc_card_t *card);
void ink_app_prepare_browser_fallback(ink_file_browser_t *browser);
