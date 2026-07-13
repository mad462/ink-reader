#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_hw.h"
#include "ink_input.h"
#include "ink_reader_core.h"
#include "ink_sd.h"

static const char *TAG = "reader";

enum { READER_PARTIAL_REFRESH_LIMIT = 50 };

static bool reader_should_cleanup(unsigned successful_page_turns) {
  return successful_page_turns >= READER_PARTIAL_REFRESH_LIMIT - 1U;
}

static bool find_changed_region(const uint8_t *previous,
                                const uint8_t *current, uint16_t width,
                                uint16_t height, ink_epd_region_t *region) {
  if (!previous || !current || !region || width == 0 || height == 0 ||
      (width & 7U) != 0)
    return false;

  int min_x = width;
  int min_y = height;
  int max_x = -1;
  int max_y = -1;
  const size_t stride = width / 8U;
  for (uint16_t y = 0; y < height; ++y) {
    for (uint16_t byte_x = 0; byte_x < stride; ++byte_x) {
      const uint8_t changed =
          previous[(size_t)y * stride + byte_x] ^
          current[(size_t)y * stride + byte_x];
      if (changed == 0) continue;
      for (uint8_t bit = 0; bit < 8U; ++bit) {
        if ((changed & (uint8_t)(0x80U >> bit)) == 0) continue;
        const int x = (int)byte_x * 8 + bit;
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if ((int)y < min_y) min_y = y;
        if ((int)y > max_y) max_y = y;
      }
    }
  }
  if (max_x < min_x || max_y < min_y) return false;
  *region = (ink_epd_region_t){.x = min_x,
                              .y = min_y,
                              .width = max_x - min_x + 1,
                              .height = max_y - min_y + 1};
  return true;
}

static bool reader_refresh_policy_self_test(void) {
  const uint8_t previous[4] = {0xff, 0xff, 0xff, 0xff};
  uint8_t current[4];
  memcpy(current, previous, sizeof(current));
  ink_epd_region_t region = {0};
  if (find_changed_region(previous, current, 16, 2, &region)) return false;
  current[0] &= (uint8_t)~(0x80U >> 3);
  current[3] &= (uint8_t)~(0x80U >> 7);
  if (!find_changed_region(previous, current, 16, 2, &region) ||
      region.x != 3 || region.y != 0 || region.width != 13 ||
      region.height != 2)
    return false;
  const uint8_t boundary_previous[2] = {0xff, 0xff};
  const uint8_t boundary_current[2] = {0x7f, 0xfe};
  return find_changed_region(boundary_previous, boundary_current, 8, 2,
                             &region) &&
         region.x == 0 && region.y == 0 && region.width == 8 &&
         region.height == 2 && !reader_should_cleanup(48) &&
         reader_should_cleanup(49);
}

static void log_stage(const char *stage, int64_t started_us) {
  ESP_LOGI(TAG, "APP_STAGE name=reader stage=%s elapsed_ms=%lld", stage,
           (long long)((esp_timer_get_time() - started_us) / 1000));
}

static void wait_for_launcher(esp_err_t input_ret) {
  while (true) {
    ink_input_snapshot_t input = {0};
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (input_ret == ESP_OK && ink_input_poll(now_ms, &input) == ESP_OK &&
        ink_input_was_pressed(&input, INK_BUTTON_BACK)) {
      ESP_LOGI(TAG, "BOOT_SWITCH from=reader to=launcher");
      const esp_err_t ret = ink_boot_switch_to_launcher();
      if (ret != ESP_OK)
        ESP_LOGE(TAG, "return to launcher failed err=%s",
                 esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void app_main(void) {
  const int64_t started_us = esp_timer_get_time();
  ESP_LOGI(TAG, "APP_START name=reader");
  if (!ink_reader_core_self_test() || !reader_refresh_policy_self_test()) {
    ESP_LOGE(TAG, "reader component self test failed");
    return;
  }
  uint8_t *framebuffer = heap_caps_malloc(INK_EPD_BUFFER_SIZE,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint8_t *previous_framebuffer = heap_caps_malloc(
      INK_EPD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!framebuffer || !previous_framebuffer) {
    ESP_LOGE(TAG, "framebuffer allocation failed current=%p previous=%p",
             framebuffer, previous_framebuffer);
    uint8_t *error_framebuffer = framebuffer ? framebuffer : previous_framebuffer;
    const esp_err_t memory_display_ret = ink_hw_init();
    const esp_err_t memory_input_ret = ink_input_init();
    if (memory_display_ret == ESP_OK && error_framebuffer) {
      ink_epd_ui_draw_status(error_framebuffer, INK_EPD_BUFFER_SIZE, "READER",
                             "MEMORY ERROR");
      (void)ink_hw_full_refresh(error_framebuffer, INK_EPD_BUFFER_SIZE);
    }
    wait_for_launcher(memory_input_ret);
    return;
  }

  esp_err_t display_ret = ink_hw_init();
  if (display_ret != ESP_OK)
    ESP_LOGE(TAG, "display init failed err=%s", esp_err_to_name(display_ret));
  esp_err_t input_ret = ink_input_init();
  if (input_ret != ESP_OK)
    ESP_LOGE(TAG, "input init failed err=%s", esp_err_to_name(input_ret));
  esp_err_t sd_ret = ink_sd_mount();
  log_stage("sd_mount_done", started_us);

  ink_reader_book_t book;
  ink_reader_book_init(&book);
  char book_path[INK_READER_PATH_MAX];
  ink_reader_scan_result_t scan_result = INK_READER_SCAN_IO_ERROR;
  if (sd_ret == ESP_OK) {
    scan_result =
        ink_reader_open_first_book(&book, book_path, sizeof(book_path));
    switch (scan_result) {
      case INK_READER_SCAN_OK:
        ESP_LOGI(TAG, "book candidate opened path=%s pages=%u", book.path,
                 (unsigned)book.page_count);
        break;
      case INK_READER_SCAN_DIR_MISSING:
        ESP_LOGW(TAG, "books directory missing path=/sdcard/books");
        break;
      case INK_READER_SCAN_EMPTY:
        ESP_LOGW(TAG, "book scan empty formats=.xtc,.xtch");
        break;
      case INK_READER_SCAN_FORMAT_ERROR:
        ESP_LOGE(TAG, "book format invalid first_candidate=%s", book_path);
        break;
      case INK_READER_SCAN_IO_ERROR:
      default:
        ESP_LOGE(TAG, "book scan I/O error");
        break;
    }
  } else {
    ESP_LOGE(TAG, "SD mount failed err=%s", esp_err_to_name(sd_ret));
  }
  log_stage("book_scan_done", started_us);
  bool book_ready = scan_result == INK_READER_SCAN_OK &&
                    ink_reader_book_load_current(
                        &book, framebuffer, INK_EPD_BUFFER_SIZE);
  if (scan_result == INK_READER_SCAN_OK && !book_ready)
    ESP_LOGE(TAG, "book first page invalid path=%s", book.path);
  if (book_ready) log_stage("first_page_decoded", started_us);

  if (!book_ready) {
    const char *message = "未找到书籍";
    if (sd_ret != ESP_OK)
      message = "SD 卡错误";
    else if (scan_result == INK_READER_SCAN_FORMAT_ERROR ||
             scan_result == INK_READER_SCAN_OK)
      message = "书籍格式错误";
    ink_epd_ui_draw_status(framebuffer, INK_EPD_BUFFER_SIZE, "READER",
                           message);
    log_stage("status_drawn", started_us);
  } else {
    ESP_LOGI(TAG, "book opened path=%s pages=%u", book.path,
             (unsigned)book.page_count);
  }
  bool screen_ready = false;
  if (display_ret == ESP_OK) {
    ESP_LOGI(TAG, "FIRST_REFRESH mode=full");
    esp_err_t ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
    if (ret != ESP_OK)
      ESP_LOGE(TAG, "status refresh failed err=%s", esp_err_to_name(ret));
    else {
      screen_ready = true;
      log_stage("first_refresh_done", started_us);
    }
  }

  unsigned successful_page_turns = 0;
  while (true) {
    ink_input_snapshot_t input = {0};
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (input_ret == ESP_OK && ink_input_poll(now_ms, &input) == ESP_OK) {
      size_t target_page = book.current_page;
      if (book_ready && ink_input_was_pressed(&input, INK_BUTTON_LEFT) &&
          target_page > 0)
        --target_page;
      if (book_ready && ink_input_was_pressed(&input, INK_BUTTON_RIGHT) &&
          target_page + 1 < book.page_count)
        ++target_page;
      if (target_page != book.current_page) {
        const size_t previous_page = book.current_page;
        memcpy(previous_framebuffer, framebuffer, INK_EPD_BUFFER_SIZE);
        if (ink_reader_book_load_page(&book, target_page, framebuffer,
                                      INK_EPD_BUFFER_SIZE)) {
          ink_epd_region_t region = {0};
          const bool changed = find_changed_region(
              previous_framebuffer, framebuffer, INK_HW_WIDTH, INK_HW_HEIGHT,
              &region);
          const bool recovery = !screen_ready;
          const bool cleanup =
              !recovery && reader_should_cleanup(successful_page_turns);
          esp_err_t ret = ESP_OK;
          if (recovery) {
            ESP_LOGI(TAG, "PAGE_REFRESH mode=recovery_full page=%u",
                     (unsigned)target_page);
            ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
          } else if (cleanup) {
            ESP_LOGI(TAG, "PAGE_REFRESH mode=cleanup_full page=%u",
                     (unsigned)target_page);
            ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
          } else if (changed) {
            ESP_LOGI(TAG,
                     "PAGE_REFRESH mode=partial page=%u x=%d y=%d w=%d h=%d",
                     (unsigned)target_page, region.x, region.y, region.width,
                     region.height);
            ret = ink_hw_partial_refresh_area(
                framebuffer, INK_EPD_BUFFER_SIZE, (uint16_t)region.x,
                (uint16_t)region.y, (uint16_t)region.width,
                (uint16_t)region.height);
          } else {
            ESP_LOGI(TAG, "PAGE_REFRESH mode=none page=%u",
                     (unsigned)target_page);
          }
          if (ret != ESP_OK) {
            ESP_LOGE(TAG, "page refresh failed err=%s", esp_err_to_name(ret));
            screen_ready = false;
            book.current_page = previous_page;
            memcpy(framebuffer, previous_framebuffer, INK_EPD_BUFFER_SIZE);
          } else {
            screen_ready = true;
            successful_page_turns =
                (cleanup || recovery) ? 0 : successful_page_turns + 1;
          }
        } else {
          ESP_LOGE(TAG, "page load failed page=%u",
                   (unsigned)target_page);
        }
      }
      if (ink_input_was_pressed(&input, INK_BUTTON_BACK)) {
        ESP_LOGI(TAG, "BOOT_SWITCH from=reader to=launcher");
        esp_err_t ret = ink_boot_switch_to_launcher();
        if (ret != ESP_OK)
          ESP_LOGE(TAG, "return to launcher failed err=%s",
                   esp_err_to_name(ret));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
