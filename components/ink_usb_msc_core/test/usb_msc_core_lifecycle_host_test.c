#include "ink_usb_msc_core.h"

#include <stdio.h>

#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_msc.h"

static esp_err_t s_deinit_result = ESP_OK;
static int s_deinit_calls;
static struct host_storage {
  int unused;
} s_storage;

esp_err_t host_sdmmc_init(void) { return ESP_OK; }

esp_err_t host_sdmmc_deinit_slot(int slot) {
  (void)slot;
  ++s_deinit_calls;
  return s_deinit_result;
}

esp_err_t sdmmc_host_init_slot(int slot,
                               const sdmmc_slot_config_t *slot_config) {
  (void)slot;
  (void)slot_config;
  return ESP_OK;
}

esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *card) {
  (void)host;
  (void)card;
  return ESP_OK;
}

esp_err_t tinyusb_driver_install(const tinyusb_config_t *config) {
  (void)config;
  return ESP_OK;
}

esp_err_t tinyusb_driver_uninstall(void) { return ESP_OK; }

esp_err_t tinyusb_msc_install_driver(
    const tinyusb_msc_driver_config_t *config) {
  (void)config;
  return ESP_OK;
}

esp_err_t tinyusb_msc_uninstall_driver(void) { return ESP_OK; }

esp_err_t tinyusb_msc_new_storage_sdmmc(
    const tinyusb_msc_storage_config_t *config,
    tinyusb_msc_storage_handle_t *handle) {
  (void)config;
  *handle = &s_storage;
  return ESP_OK;
}

esp_err_t tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t handle) {
  (void)handle;
  return ESP_OK;
}

#include "../ink_usb_msc_state.c"
#include "../ink_usb_msc_core.c"

static int expect(bool condition, const char *message) {
  if (condition) return 0;
  fprintf(stderr, "%s\n", message);
  return 1;
}

int main(void) {
  if (expect(ink_usb_msc_core_start() == ESP_OK,
             "core start must succeed with host doubles"))
    return 1;

  s_deinit_result = ESP_FAIL;
  if (expect(ink_usb_msc_core_stop() == ESP_FAIL,
             "SDMMC deinit failure must propagate") ||
      expect(ink_usb_msc_core_state() == INK_USB_MSC_ERROR,
             "SDMMC deinit failure must enter error state") ||
      expect(!ink_usb_msc_core_can_return_launcher(),
             "SDMMC deinit failure must block launcher return"))
    return 1;

  s_deinit_result = ESP_OK;
  if (expect(ink_usb_msc_core_stop() == ESP_OK,
             "retry must release retained SDMMC state") ||
      expect(s_deinit_calls == 2, "retry must call SDMMC deinit again") ||
      expect(ink_usb_msc_core_can_return_launcher(),
             "successful retry must allow launcher return"))
    return 1;

  puts("PASS: USB MSC lifecycle host test");
  return 0;
}
