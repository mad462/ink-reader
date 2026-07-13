#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_fonts.h"
#include "ink_hw.h"
#include "ink_input.h"
#include "ink_photo_core.h"
#include "ink_sd.h"

static const char *TAG = "photo";

enum {
  PHOTO_PARTIAL_REFRESH_LIMIT = 50,
  PHOTO_FONT_DONE = BIT0,
  PHOTO_FONT_READY = BIT1,
};

enum photo_view {
  LIST,
  PREVIEW,
};

static ink_epd_photo_row_t photo_rows[INK_PHOTO_MAX_ITEMS];
static ink_cpfont_t s_menu_font;
static ink_cpfont_t s_footer_font;
static ink_epd_ui_fonts_t s_photo_fonts;
static EventGroupHandle_t s_font_events;
static bool s_font_sync_complete;
static int64_t s_started_us;

typedef struct {
  uint8_t *lsb;
  uint8_t *msb;
} photo_decode_context_t;

typedef struct {
  size_t count;
  size_t target_index;
  int direction;
  uint32_t generation;
  uint32_t active_generation;
  bool input_ready;
} photo_navigation_t;

typedef enum {
  PHOTO_REFRESH_FAILED,
  PHOTO_REFRESH_SHOWN,
  PHOTO_REFRESH_SUPERSEDED,
} photo_refresh_result_t;

static int64_t elapsed_ms(void) {
  return (esp_timer_get_time() - s_started_us) / 1000;
}

static void log_stage(const char *stage) {
  ESP_LOGI(TAG, "APP_STAGE name=photo stage=%s elapsed_ms=%lld", stage,
           (long long)elapsed_ms());
}

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

static bool photo_navigation_apply(photo_navigation_t *navigation,
                                   bool previous_pressed,
                                   bool next_pressed) {
  if (!navigation || navigation->count == 0 ||
      (!previous_pressed && !next_pressed))
    return false;
  navigation->direction = previous_pressed ? -1 : 1;
  navigation->target_index =
      previous_pressed
          ? (navigation->target_index == 0 ? navigation->count - 1
                                           : navigation->target_index - 1)
          : (navigation->target_index + 1) % navigation->count;
  ++navigation->generation;
  return true;
}

static void photo_navigation_follow(photo_navigation_t *navigation,
                                    size_t index, int direction) {
  if (!navigation || navigation->count == 0) return;
  navigation->target_index = index < navigation->count ? index : 0;
  navigation->direction = direction < 0 ? -1 : 1;
}

static bool photo_navigation_commit_displayed(
    photo_navigation_t *navigation, size_t displayed_index,
    uint32_t displayed_generation) {
  if (!navigation || displayed_index >= navigation->count) return false;
  if (navigation->generation == displayed_generation)
    photo_navigation_follow(navigation, displayed_index,
                            navigation->direction);
  return navigation->target_index == displayed_index;
}

static bool photo_navigation_poll(void *context) {
  photo_navigation_t *navigation = context;
  if (!navigation) return false;
  if (navigation->input_ready) {
    ink_input_snapshot_t input = {0};
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (ink_input_poll(now, &input) == ESP_OK) {
      const bool previous_pressed =
          ink_input_was_pressed(&input, INK_BUTTON_LEFT);
      const bool next_pressed =
          ink_input_was_pressed(&input, INK_BUTTON_RIGHT);
      if (photo_navigation_apply(navigation, previous_pressed, next_pressed))
        ESP_LOGI(TAG, "PHOTO_NAV target=%u generation=%u direction=%s",
                 (unsigned)navigation->target_index,
                 (unsigned)navigation->generation,
                 navigation->direction < 0 ? "left" : "right");
    }
  }
  return navigation->generation != navigation->active_generation;
}

static bool photo_navigation_self_test(void) {
  photo_navigation_t navigation = {
      .count = 3, .target_index = 0, .direction = 1};
  if (!photo_navigation_apply(&navigation, true, false) ||
      navigation.target_index != 2 || navigation.direction != -1 ||
      navigation.generation != 1)
    return false;
  navigation.active_generation = navigation.generation;
  if (!photo_navigation_apply(&navigation, false, true) ||
      navigation.target_index != 0 || navigation.direction != 1 ||
      navigation.generation != 2 ||
      navigation.generation == navigation.active_generation)
    return false;
  photo_navigation_follow(&navigation, 1, -1);
  if (navigation.target_index != 1 || navigation.direction != -1)
    return false;

  photo_navigation_t skipped = {
      .count = 3, .target_index = 1, .direction = 1, .generation = 4};
  if (!photo_navigation_commit_displayed(&skipped, 2, 4) ||
      skipped.target_index != 2 ||
      !photo_navigation_apply(&skipped, false, true) ||
      skipped.target_index != 0)
    return false;

  photo_navigation_t newer = {
      .count = 3, .target_index = 0, .direction = 1, .generation = 5};
  return !photo_navigation_commit_displayed(&newer, 2, 4) &&
         newer.target_index == 0;
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
  ink_epd_ui_draw_status(lsb, INK_PHOTO_PLANE_SIZE, "PHOTO", message);
  esp_err_t ret = ink_hw_full_refresh(lsb, INK_PHOTO_PLANE_SIZE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "%s status refresh failed error=%s", context,
             esp_err_to_name(ret));
    return false;
  }
  return true;
}

static bool try_decode_item(const ink_photo_item_t *item, void *context) {
  photo_decode_context_t *decode = context;
  if (!item || !decode || !decode->lsb || !decode->msb) return false;
  if (!ink_photo_decode_bmp(item->path, decode->lsb, INK_PHOTO_PLANE_SIZE,
                            decode->msb, INK_PHOTO_PLANE_SIZE)) {
    ESP_LOGW(TAG, "image decode skipped path=%s", item->path);
    return false;
  }
  return true;
}

static photo_refresh_result_t refresh_decoded_photo(
    const ink_photo_catalog_t *catalog, size_t index, const uint8_t *lsb,
    const uint8_t *msb, photo_navigation_t *navigation) {
  if (!catalog || !lsb || !msb || !navigation || index >= catalog->count)
    return PHOTO_REFRESH_FAILED;
  navigation->active_generation = navigation->generation;
  ESP_LOGI(TAG, "PHOTO_REFRESH_START index=%u generation=%u",
           (unsigned)index, (unsigned)navigation->active_generation);
  esp_err_t ret = ink_hw_gray_refresh_with_poll(
      lsb, INK_PHOTO_PLANE_SIZE, msb, INK_PHOTO_PLANE_SIZE,
      photo_navigation_poll, navigation);
  if (ret == ESP_ERR_NOT_FINISHED) {
    ESP_LOGI(TAG, "PHOTO_REFRESH_SUPERSEDED index=%u latest=%u",
             (unsigned)index, (unsigned)navigation->target_index);
    return PHOTO_REFRESH_SUPERSEDED;
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "photo refresh failed path=%s error=%s",
             catalog->items[index].path, esp_err_to_name(ret));
    return PHOTO_REFRESH_FAILED;
  }
  ESP_LOGI(TAG, "PHOTO_REFRESH_DONE index=%u generation=%u pending=%u",
           (unsigned)index, (unsigned)navigation->active_generation,
           (unsigned)navigation->target_index);
  return PHOTO_REFRESH_SHOWN;
}

static bool show_decodable_photo(const ink_photo_catalog_t *catalog,
                                 uint8_t *lsb, uint8_t *msb,
                                 photo_navigation_t *navigation,
                                 size_t *out_index) {
  if (!catalog || !navigation || !out_index) return false;
  photo_decode_context_t decode = {.lsb = lsb, .msb = msb};
  bool shown = false;
  while (true) {
    const size_t target_index = navigation->target_index;
    const uint32_t generation = navigation->generation;
    const int direction = navigation->direction;
    size_t decoded_index;
    if (!ink_photo_catalog_find_decodable(
            catalog, target_index, direction, try_decode_item, &decode,
            &decoded_index))
      return shown;

    const photo_refresh_result_t result = refresh_decoded_photo(
        catalog, decoded_index, lsb, msb, navigation);
    if (result == PHOTO_REFRESH_FAILED) return shown;
    if (result == PHOTO_REFRESH_SHOWN) {
      *out_index = decoded_index;
      shown = true;
      if (photo_navigation_commit_displayed(navigation, decoded_index,
                                            generation))
        return true;
    }
    if (navigation->generation == generation) return shown;
    ESP_LOGI(TAG, "PHOTO_COALESCE replaced=%u latest=%u",
             (unsigned)target_index, (unsigned)navigation->target_index);
  }
}

static EventBits_t photo_font_state(void) {
  if (s_font_events) return xEventGroupGetBits(s_font_events);
  if (!s_font_sync_complete) return 0;
  return PHOTO_FONT_DONE |
         (ink_cpfont_is_loaded(&s_menu_font) ? PHOTO_FONT_READY : 0);
}

static EventBits_t wait_for_photo_fonts(void) {
  if (!s_font_events) return photo_font_state();
  return xEventGroupWaitBits(s_font_events, PHOTO_FONT_DONE, pdFALSE, pdTRUE,
                             portMAX_DELAY);
}

static void load_photo_fonts(void) {
  const bool menu_ok = ink_fonts_load(&s_menu_font, INK_FONT_MENU);
  const bool footer_ok = ink_fonts_load(&s_footer_font, INK_FONT_FOOTER);
  if (menu_ok) {
    s_photo_fonts.title = &s_menu_font;
    s_photo_fonts.body = &s_menu_font;
    ESP_LOGI(TAG, "menu font loaded path=%s", s_menu_font.path);
  } else {
    ESP_LOGW(TAG, "menu font not found; photo list unavailable");
  }
  if (footer_ok) {
    s_photo_fonts.footer = &s_footer_font;
    ESP_LOGI(TAG, "footer font loaded path=%s", s_footer_font.path);
  } else {
    ESP_LOGW(TAG, "footer font not found; using ASCII photo metadata");
  }
  s_font_sync_complete = true;
  if (s_font_events)
    xEventGroupSetBits(s_font_events,
                       PHOTO_FONT_DONE | (menu_ok ? PHOTO_FONT_READY : 0));
}

static void photo_font_task(void *unused) {
  (void)unused;
  log_stage("font_start");
  load_photo_fonts();
  log_stage("font_done");
  vTaskDelete(NULL);
}

void app_main(void) {
  s_started_us = esp_timer_get_time();
  ESP_LOGI(TAG, "APP_START name=photo");
  const bool core_ready = ink_fonts_self_test() && ink_hw_self_test() &&
                          ink_photo_core_self_test() &&
                          photo_policy_self_test() &&
                          photo_navigation_self_test();
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
    log_stage("sd_mount_done");
    if (sd_ret == ESP_OK) {
      catalog_ready = ink_photo_catalog_load(catalog);
      log_stage("catalog_loaded");
      if (!catalog_ready) {
        ESP_LOGE(TAG, "catalog load failed path=%s", INK_PHOTO_DIR);
      }
    }
  }
  const size_t count = photo_catalog_count(catalog, catalog_ready);
  build_photo_rows(catalog, count);
  photo_navigation_t navigation = {
      .count = count,
      .target_index = 0,
      .direction = 1,
      .input_ready = input_ret == ESP_OK,
  };

  enum photo_view view = PREVIEW;
  size_t current = 0;
  unsigned successful_partial_count = 0;
  bool force_full_next = false;
  bool photo_ready = false;
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
    } else if (count == 0) {
      (void)show_status_page(lsb, "未找到图片", "empty catalog");
    } else {
      photo_decode_context_t decode = {.lsb = lsb, .msb = msb};
      if (!ink_photo_catalog_find_decodable(catalog, 0, 1, try_decode_item,
                                            &decode, &current)) {
        (void)show_status_page(lsb, "未找到可显示图片", "decode catalog");
      } else {
        photo_navigation_follow(&navigation, current, 1);
        log_stage("first_photo_decoded");
        s_font_events = xEventGroupCreate();
        const bool font_task_started =
            s_font_events &&
            xTaskCreate(photo_font_task, "photo_font", 4096, NULL,
                        tskIDLE_PRIORITY + 1, NULL) == pdPASS;
        if (!font_task_started)
          ESP_LOGW(TAG, "font task unavailable; loading after first refresh");

        const photo_refresh_result_t first_result = refresh_decoded_photo(
            catalog, current, lsb, msb, &navigation);
        photo_ready = first_result == PHOTO_REFRESH_SHOWN;
        if (first_result == PHOTO_REFRESH_SUPERSEDED ||
            navigation.generation != navigation.active_generation) {
          size_t shown_index = current;
          if (show_decodable_photo(catalog, lsb, msb, &navigation,
                                   &shown_index)) {
            current = shown_index;
            photo_ready = true;
          }
        }
        if (photo_ready)
          log_stage("first_refresh_done");
        else
          (void)show_status_page(lsb, "IMAGE ERROR", "first refresh");
        if (!font_task_started) {
          log_stage("font_start");
          load_photo_fonts();
          log_stage("font_done");
        }
      }
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
          sd_ret != ESP_OK || !catalog_ready || !photo_ready) {
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
            photo_navigation_follow(
                &navigation, current, previous_pressed ? -1 : 1);
            force_full_next = !refreshed;
            if (!refreshed) {
              draw_photo_list(catalog, catalog_ready, current, lsb);
            }
          }
        }

        if (count > 0 && confirm_pressed) {
          photo_navigation_follow(&navigation, current, 1);
          size_t shown_index = current;
          const bool shown = show_decodable_photo(
              catalog, lsb, msb, &navigation, &shown_index);
          if (!shown) {
            (void)show_status_page(lsb, "未找到可显示图片", "image open");
            force_full_next = true;
          } else {
            current = shown_index;
            view = PREVIEW;
          }
        }
      } else {
        EventBits_t font_state = photo_font_state();
        if ((font_state & PHOTO_FONT_DONE) == 0) {
          if (confirm_pressed) {
            (void)show_status_page(lsb, "正在加载", "font loading");
            font_state = wait_for_photo_fonts();
            if ((font_state & PHOTO_FONT_READY) != 0) {
              draw_photo_list(catalog, catalog_ready, current, lsb);
              const bool refreshed = full_refresh_photo_list(
                  lsb, &successful_partial_count, "font ready list");
              view = photo_committed_view(view, LIST, refreshed);
              force_full_next = !refreshed;
            } else {
              (void)show_status_page(lsb, "字体不可用", "font unavailable");
            }
          } else if (previous_pressed || next_pressed) {
            ESP_LOGI(TAG, "photo navigation deferred while font loads");
          }
          vTaskDelay(pdMS_TO_TICKS(20));
          continue;
        }

        if (count > 0 && (previous_pressed || next_pressed)) {
          (void)photo_navigation_apply(&navigation, previous_pressed,
                                       next_pressed);
          size_t shown_index = current;
          const bool shown = show_decodable_photo(
              catalog, lsb, msb, &navigation, &shown_index);
          if (!shown) {
            ESP_LOGW(TAG, "no decodable photo; keeping index=%u",
                     (unsigned)current);
          } else
            current = shown_index;
        }

        if (confirm_pressed) {
          if ((font_state & PHOTO_FONT_READY) != 0) {
            draw_photo_list(catalog, catalog_ready, current, lsb);
            const bool refreshed = full_refresh_photo_list(
                lsb, &successful_partial_count, "preview return");
            view = photo_committed_view(view, LIST, refreshed);
            force_full_next = !refreshed;
          } else {
            (void)show_status_page(lsb, "字体不可用", "font unavailable");
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
