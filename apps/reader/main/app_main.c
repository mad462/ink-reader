#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ink_boot_switch.h"
#include "ink_epd_ui.h"
#include "ink_hw.h"
#include "ink_input.h"
#include "ink_reader_core.h"
#include "ink_sd.h"

static const char *TAG = "reader";

static void log_stage(const char *stage, int64_t started_us) {
  ESP_LOGI(TAG, "APP_STAGE name=reader stage=%s elapsed_ms=%lld", stage,
           (long long)((esp_timer_get_time() - started_us) / 1000));
}

void app_main(void) {
  const int64_t started_us = esp_timer_get_time();
  ESP_LOGI(TAG, "APP_START name=reader");
  if (!ink_reader_core_self_test()) {
    ESP_LOGE(TAG, "reader component self test failed");
    return;
  }
  uint8_t *framebuffer = heap_caps_malloc(INK_EPD_BUFFER_SIZE,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!framebuffer) {
    ESP_LOGE(TAG, "framebuffer allocation failed");
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
  if (display_ret == ESP_OK) {
    esp_err_t ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
    if (ret != ESP_OK)
      ESP_LOGE(TAG, "status refresh failed err=%s", esp_err_to_name(ret));
    else
      log_stage("first_refresh_done", started_us);
  }

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
        if (ink_reader_book_load_page(&book, target_page, framebuffer,
                                      INK_EPD_BUFFER_SIZE)) {
          esp_err_t ret = ink_hw_full_refresh(framebuffer, INK_EPD_BUFFER_SIZE);
          if (ret != ESP_OK)
            ESP_LOGE(TAG, "page refresh failed err=%s", esp_err_to_name(ret));
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
