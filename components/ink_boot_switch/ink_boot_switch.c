#include "ink_boot_switch.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

static const char *TAG = "ink_boot_switch";

static esp_err_t switch_to(const char *label) {
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
  if (partition == NULL) {
    ESP_LOGE(TAG, "target partition not found label=%s", label);
    return ESP_ERR_NOT_FOUND;
  }

  esp_err_t ret = esp_ota_set_boot_partition(partition);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "set boot partition failed label=%s err=%s", label,
             esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "boot partition selected label=%s offset=0x%lx", label,
           (unsigned long)partition->address);
  esp_restart();
  return ESP_OK;
}

esp_err_t ink_boot_switch_to_launcher(void) { return switch_to("launcher"); }
esp_err_t ink_boot_switch_to_reader(void) { return switch_to("reader"); }
esp_err_t ink_boot_switch_to_photo(void) { return switch_to("photo"); }
esp_err_t ink_boot_switch_to_usb_msc(void) { return switch_to("usb_msc"); }
esp_err_t ink_boot_switch_to_wifi_setup(void) { return switch_to("wifi_setup"); }
