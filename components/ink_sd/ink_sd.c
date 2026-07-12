#include "ink_sd.h"

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "ink_sd";
static sdmmc_card_t *s_card;

esp_err_t ink_sd_mount(void)
{
    if (s_card != NULL) return ESP_OK;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = 40;
    slot.cmd = 39;
    slot.d0 = 41;
    slot.d1 = 42;
    slot.d2 = 48;
    slot.d3 = 38;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(INK_SD_MOUNT_POINT, &host, &slot, &mount, &s_card);
    if (ret != ESP_OK) {
        s_card = NULL;
        ESP_LOGW(TAG, "mount failed path=%s err=%s", INK_SD_MOUNT_POINT, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "mounted path=%s", INK_SD_MOUNT_POINT);
    return ESP_OK;
}

void ink_sd_unmount(void)
{
    if (s_card != NULL) {
        esp_vfs_fat_sdcard_unmount(INK_SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }
}

bool ink_sd_is_mounted(void) { return s_card != NULL; }
