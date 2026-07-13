#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
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

static bool photo_should_full_refresh(unsigned successful_partial_count) {
  return successful_partial_count >= PHOTO_PARTIAL_REFRESH_LIMIT;
}

static bool photo_policy_self_test(void) {
  return !photo_should_full_refresh(PHOTO_PARTIAL_REFRESH_LIMIT - 1) &&
         photo_should_full_refresh(PHOTO_PARTIAL_REFRESH_LIMIT);
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
  ink_epd_ui_draw_photo_list(lsb, INK_PHOTO_PLANE_SIZE,
                             count > 0 ? photo_rows : NULL, count, selected);
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

static void refresh_photo_list(const uint8_t *lsb, size_t previous,
                               size_t selected, size_t count,
                               unsigned *successful_partial_count) {
  if (!lsb || !successful_partial_count || count == 0) return;

  if (photo_should_full_refresh(*successful_partial_count)) {
    (void)full_refresh_photo_list(lsb, successful_partial_count, "selection");
    return;
  }

  const ink_epd_region_t region =
      ink_epd_ui_photo_list_selection_region(previous, selected, count);
  esp_err_t ret = ink_hw_partial_refresh_area(
      lsb, INK_PHOTO_PLANE_SIZE, (uint16_t)region.x, (uint16_t)region.y,
      (uint16_t)region.width, (uint16_t)region.height);
  if (ret == ESP_OK) {
    ++*successful_partial_count;
    return;
  }

  ESP_LOGE(TAG,
           "selection partial refresh failed x=%d y=%d w=%d h=%d error=%s",
           region.x, region.y, region.width, region.height,
           esp_err_to_name(ret));
  (void)full_refresh_photo_list(lsb, successful_partial_count,
                                "selection fallback");
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
  if (!ink_photo_core_self_test() || !photo_policy_self_test()) {
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

  esp_err_t sd_ret = catalog ? ink_sd_mount() : ESP_ERR_NO_MEM;
  bool catalog_ready =
      sd_ret == ESP_OK && catalog && ink_photo_catalog_load(catalog);
  const size_t count = photo_catalog_count(catalog, catalog_ready);
  build_photo_rows(catalog, count);

  enum photo_view view = LIST;
  size_t current = 0;
  unsigned successful_partial_count = 0;
  if (display_ret == ESP_OK && lsb) {
    if (sd_ret != ESP_OK) {
      ink_epd_ui_draw_status(lsb, INK_PHOTO_PLANE_SIZE, "PHOTO",
                             "SD CARD ERROR");
      (void)full_refresh_photo_list(lsb, &successful_partial_count, "status");
    } else {
      draw_photo_list(catalog, catalog_ready, current, lsb);
      (void)full_refresh_photo_list(lsb, &successful_partial_count, "initial");
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

      const bool previous_pressed =
          ink_input_was_pressed(&input, INK_BUTTON_LEFT);
      const bool next_pressed =
          ink_input_was_pressed(&input, INK_BUTTON_RIGHT);
      const bool confirm_pressed =
          ink_input_was_pressed(&input, INK_BUTTON_CONFIRM);

      if (view == LIST) {
        if (count > 0 && (previous_pressed || next_pressed)) {
          const size_t previous = current;
          current = previous_pressed
                        ? (current == 0 ? count - 1 : current - 1)
                        : (current + 1) % count;
          draw_photo_list(catalog, catalog_ready, current, lsb);
          refresh_photo_list(lsb, previous, current, count,
                             &successful_partial_count);
        }

        if (count > 0 && confirm_pressed) {
          if (show_photo(catalog, current, lsb, msb)) {
            view = PREVIEW;
          } else {
            draw_photo_list(catalog, catalog_ready, current, lsb);
            (void)full_refresh_photo_list(
                lsb, &successful_partial_count, "list restore");
          }
        }
      } else {
        if (count > 0 && (previous_pressed || next_pressed)) {
          const size_t candidate =
              previous_pressed ? (current == 0 ? count - 1 : current - 1)
                               : (current + 1) % count;
          if (show_photo(catalog, candidate, lsb, msb)) {
            current = candidate;
          } else {
            ESP_LOGE(TAG, "keeping current photo index=%u",
                     (unsigned)current);
            if (!show_photo(catalog, current, lsb, msb)) {
              ESP_LOGE(TAG, "current photo restore failed index=%u",
                       (unsigned)current);
            }
          }
        }

        if (confirm_pressed) {
          view = LIST;
          draw_photo_list(catalog, catalog_ready, current, lsb);
          (void)full_refresh_photo_list(lsb, &successful_partial_count,
                                        "preview return");
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
