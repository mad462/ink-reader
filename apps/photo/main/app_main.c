#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_fonts.h"
#include "ink_hw.h"
#include "ink_input.h"
#include "ink_photo_core.h"
#include "ink_sd.h"

static const char *TAG = "photo";

enum { PHOTO_PARTIAL_REFRESH_LIMIT = 50 };

enum photo_view {
  LIST,
  PREVIEW,
};

static ink_epd_photo_row_t photo_rows[INK_PHOTO_MAX_ITEMS];
static ink_cpfont_t s_menu_font;
static ink_cpfont_t s_footer_font;
static ink_epd_ui_fonts_t s_photo_fonts;

static bool photo_should_full_refresh(unsigned successful_partial_count) {
  return successful_partial_count >= PHOTO_PARTIAL_REFRESH_LIMIT;
}

static enum photo_view photo_committed_view(enum photo_view current,
                                            enum photo_view candidate,
                                            bool refresh_succeeded) {
  return refresh_succeeded ? candidate : current;
}

static size_t photo_committed_index(size_t current, size_t candidate,
                                    bool refresh_succeeded) {
  return refresh_succeeded ? candidate : current;
}

static bool photo_policy_self_test(void) {
  return !photo_should_full_refresh(PHOTO_PARTIAL_REFRESH_LIMIT - 1) &&
         photo_should_full_refresh(PHOTO_PARTIAL_REFRESH_LIMIT) &&
         photo_committed_view(PREVIEW, LIST, false) == PREVIEW &&
         photo_committed_view(PREVIEW, LIST, true) == LIST &&
         photo_committed_index(3, 4, false) == 3 &&
         photo_committed_index(3, 4, true) == 4;
}

static size_t photo_catalog_count(const ink_photo_catalog_t *catalog,
                                  bool catalog_ready) {
  if (!catalog || !catalog_ready) return 0;
  return catalog->count <= INK_PHOTO_MAX_ITEMS ? catalog->count
                                               : INK_PHOTO_MAX_ITEMS;
}

static void build_photo_rows(const ink_photo_catalog_t *catalog, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    photo_rows[i].name = catalog->items[i].name;
  }
}

static void draw_photo_list(const ink_photo_catalog_t *catalog,
                            bool catalog_ready, size_t selected, uint8_t *lsb) {
  if (!lsb) return;
  const size_t count = photo_catalog_count(catalog, catalog_ready);
  ink_epd_ui_draw_photo_list_with_fonts(
      lsb, INK_PHOTO_PLANE_SIZE, count > 0 ? photo_rows : NULL, count,
      selected, &s_photo_fonts);
}

static bool full_refresh_photo_list(const uint8_t *lsb,
                                    unsigned *successful_partial_count,
                                    const char *context) {
  if (!lsb) return false;
  esp_err_t ret = ink_hw_full_refresh(lsb, INK_PHOTO_PLANE_SIZE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "%s full refresh failed error=%s", context,
             esp_err_to_name(ret));
    return false;
  }
  if (successful_partial_count) *successful_partial_count = 0;
  return true;
}

static bool refresh_photo_list(const uint8_t *lsb, size_t previous,
                               size_t selected, size_t count,
                               unsigned *successful_partial_count,
                               bool force_full) {
  if (!lsb || !successful_partial_count || count == 0) return false;

  if (force_full || photo_should_full_refresh(*successful_partial_count)) {
    return full_refresh_photo_list(lsb, successful_partial_count,
                                   "selection");
  }

  const ink_epd_region_t region =
      ink_epd_ui_photo_list_selection_region(previous, selected, count);
  esp_err_t ret = ink_hw_partial_refresh_area(
      lsb, INK_PHOTO_PLANE_SIZE, (uint16_t)region.x, (uint16_t)region.y,
      (uint16_t)region.width, (uint16_t)region.height);
  if (ret == ESP_OK) {
    ++*successful_partial_count;
    return true;
  }

  ESP_LOGE(TAG,
           "selection partial refresh failed x=%d y=%d w=%d h=%d error=%s",
           region.x, region.y, region.width, region.height,
           esp_err_to_name(ret));
  return full_refresh_photo_list(lsb, successful_partial_count,
                                 "selection fallback");
}

static bool show_status_page(uint8_t *lsb, const char *message,
                             const char *context) {
  if (!lsb) return false;
  ink_epd_ui_draw_status_with_fonts(
      lsb, INK_PHOTO_PLANE_SIZE,
      ink_cpfont_is_loaded(s_photo_fonts.title) ? "相册" : "PHOTO", message,
      &s_photo_fonts);
  esp_err_t ret = ink_hw_full_refresh(lsb, INK_PHOTO_PLANE_SIZE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "%s status refresh failed error=%s", context,
             esp_err_to_name(ret));
    return false;
  }
  return true;
}

static bool show_photo(const ink_photo_catalog_t *catalog, size_t index,
                       uint8_t *lsb, uint8_t *msb) {
  if (!catalog || !lsb || !msb || index >= catalog->count) return false;
  if (!ink_photo_decode_bmp(catalog->items[index].path, lsb,
                            INK_PHOTO_PLANE_SIZE, msb,
                            INK_PHOTO_PLANE_SIZE)) {
    ESP_LOGE(TAG, "image decode failed path=%s", catalog->items[index].path);
    return false;
  }

  ESP_LOGI(TAG, "PHOTO_REFRESH mode=full_gray");
  esp_err_t ret =
      ink_hw_gray_refresh(lsb, INK_PHOTO_PLANE_SIZE, msb, INK_PHOTO_PLANE_SIZE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "photo refresh failed path=%s error=%s",
             catalog->items[index].path, esp_err_to_name(ret));
    return false;
  }
  ESP_LOGI(TAG, "photo shown path=%s", catalog->items[index].path);
  return true;
}

void app_main(void) {
  ESP_LOGI(TAG, "APP_START name=photo");
  const bool core_ready = ink_fonts_self_test() && ink_hw_self_test() &&
                          ink_photo_core_self_test() &&
                          photo_policy_self_test();
  if (!core_ready) {
    ESP_LOGE(TAG, "photo self test failed");
  }

  esp_err_t input_ret = ink_input_init();
  if (input_ret != ESP_OK) {
    ESP_LOGE(TAG, "input init failed err=%s", esp_err_to_name(input_ret));
  }
  esp_err_t display_ret = ink_hw_init();
  if (display_ret != ESP_OK) {
    ESP_LOGE(TAG, "display init failed err=%s", esp_err_to_name(display_ret));
  }

  uint8_t *lsb = heap_caps_malloc(INK_PHOTO_PLANE_SIZE,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint8_t *msb = heap_caps_malloc(INK_PHOTO_PLANE_SIZE,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  ink_photo_catalog_t *catalog = heap_caps_calloc(
      1, sizeof(*catalog), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!lsb || !msb || !catalog) {
    ESP_LOGE(TAG, "photo buffers allocation failed");
  }

  const bool resources_ready = lsb && msb && catalog;
  esp_err_t sd_ret = ESP_ERR_INVALID_STATE;
  bool catalog_ready = false;
  if (core_ready && resources_ready) {
    sd_ret = ink_sd_mount();
    if (sd_ret == ESP_OK) {
      if (ink_fonts_load(&s_menu_font, INK_FONT_MENU)) {
        s_photo_fonts.title = &s_menu_font;
        s_photo_fonts.body = &s_menu_font;
        ESP_LOGI(TAG, "menu font loaded path=%s", s_menu_font.path);
      } else {
        ESP_LOGW(TAG, "menu font not found; using ASCII photo UI");
      }
      if (ink_fonts_load(&s_footer_font, INK_FONT_FOOTER)) {
        s_photo_fonts.footer = &s_footer_font;
        ESP_LOGI(TAG, "footer font loaded path=%s", s_footer_font.path);
      } else {
        ESP_LOGW(TAG, "footer font not found; using ASCII photo metadata");
      }
      catalog_ready = ink_photo_catalog_load(catalog);
      if (!catalog_ready) {
        ESP_LOGE(TAG, "catalog load failed path=%s", INK_PHOTO_DIR);
      }
    }
  }
  const size_t count = photo_catalog_count(catalog, catalog_ready);
  build_photo_rows(catalog, count);

  enum photo_view view = LIST;
  size_t current = 0;
  unsigned successful_partial_count = 0;
  bool force_full_next = false;
  if (display_ret == ESP_OK && lsb) {
    if (!core_ready) {
      (void)show_status_page(lsb, "COMPONENT ERROR", "component");
    } else if (!resources_ready) {
      (void)show_status_page(lsb, "MEMORY ERROR", "memory");
    } else if (sd_ret != ESP_OK) {
      ESP_LOGE(TAG, "SD mount failed error=%s", esp_err_to_name(sd_ret));
      (void)show_status_page(lsb, "SD CARD ERROR", "SD mount");
    } else if (!catalog_ready) {
      (void)show_status_page(lsb, "CATALOG ERROR", "catalog");
    } else {
      draw_photo_list(catalog, catalog_ready, current, lsb);
      force_full_next = !full_refresh_photo_list(
          lsb, &successful_partial_count, "initial");
    }
  }

  while (true) {
    ink_input_snapshot_t input = {0};
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (input_ret == ESP_OK && ink_input_poll(now, &input) == ESP_OK) {
      if (ink_input_was_pressed(&input, INK_BUTTON_BACK)) {
        ESP_LOGI(TAG, "BOOT_SWITCH from=photo to=launcher");
        esp_err_t ret = ink_boot_switch_to_launcher();
        if (ret != ESP_OK) {
          ESP_LOGE(TAG, "return to launcher failed err=%s",
                   esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        continue;
      }

      if (!core_ready || display_ret != ESP_OK || !resources_ready ||
          sd_ret != ESP_OK || !catalog_ready) {
        vTaskDelay(pdMS_TO_TICKS(20));
        continue;
      }

      const bool previous_pressed =
          ink_input_was_pressed(&input, INK_BUTTON_LEFT);
      const bool next_pressed =
          ink_input_was_pressed(&input, INK_BUTTON_RIGHT);
      const bool confirm_pressed =
          ink_input_was_pressed(&input, INK_BUTTON_CONFIRM);

      if (view == LIST) {
        if (count > 0 && (previous_pressed || next_pressed)) {
          const size_t previous = current;
          const size_t candidate =
              previous_pressed ? (current == 0 ? count - 1 : current - 1)
                               : (current + 1) % count;
          if (candidate != current) {
            draw_photo_list(catalog, catalog_ready, candidate, lsb);
            const bool refreshed = refresh_photo_list(
                lsb, previous, candidate, count, &successful_partial_count,
                force_full_next);
            current = photo_committed_index(current, candidate, refreshed);
            force_full_next = !refreshed;
            if (!refreshed) {
              draw_photo_list(catalog, catalog_ready, current, lsb);
            }
          }
        }

        if (count > 0 && confirm_pressed) {
          const bool shown = show_photo(catalog, current, lsb, msb);
          view = photo_committed_view(view, PREVIEW, shown);
          if (!shown) {
            (void)show_status_page(lsb, "IMAGE ERROR", "image open");
            force_full_next = true;
          }
        }
      } else {
        if (count > 0 && (previous_pressed || next_pressed)) {
          const size_t candidate =
              previous_pressed ? (current == 0 ? count - 1 : current - 1)
                               : (current + 1) % count;
          const bool shown = show_photo(catalog, candidate, lsb, msb);
          current = photo_committed_index(current, candidate, shown);
          if (!shown) {
            ESP_LOGE(TAG, "keeping current photo index=%u",
                     (unsigned)current);
            (void)show_status_page(lsb, "IMAGE ERROR", "image switch");
            force_full_next = true;
          }
        }

        if (confirm_pressed) {
          draw_photo_list(catalog, catalog_ready, current, lsb);
          const bool refreshed = full_refresh_photo_list(
              lsb, &successful_partial_count, "preview return");
          view = photo_committed_view(view, LIST, refreshed);
          force_full_next = !refreshed;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
