#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "ink_app_priv.h"

esp_err_t ink_app_persist_state(const ink_app_state_t *state);
bool ink_app_should_auto_resume_reader(const ink_app_state_t *state);
bool ink_app_load_book_from_state(ink_ui_model_t *model);
bool ink_app_open_fixed_sample_book(ink_ui_model_t *model);
bool ink_app_load_reader_font(ink_cpfont_t *font);
bool ink_app_load_footer_font(ink_cpfont_t *font);
uint8_t *ink_app_alloc_display_buffer(const char *name, size_t length);
esp_err_t ink_app_mount_tf_card(void);
void ink_app_prepare_browser_fallback(ink_file_browser_t *browser);
