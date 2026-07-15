#ifndef SDMMC_HOST_HOST_H
#define SDMMC_HOST_HOST_H

#include <stdint.h>

#include "esp_err.h"

#define SDMMC_HOST_FLAG_DEINIT_ARG (1U << 0)
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP (1U << 0)
#define SDMMC_FREQ_DEFAULT 20000

typedef struct {
  uint32_t flags;
  int slot;
  int max_freq_khz;
  esp_err_t (*init)(void);
  union {
    esp_err_t (*deinit)(void);
    esp_err_t (*deinit_p)(int slot);
  };
} sdmmc_host_t;

typedef struct {
  int width;
  int clk;
  int cmd;
  int d0;
  int d1;
  int d2;
  int d3;
  uint32_t flags;
} sdmmc_slot_config_t;

esp_err_t host_sdmmc_init(void);
esp_err_t host_sdmmc_deinit_slot(int slot);
esp_err_t sdmmc_host_init_slot(int slot,
                               const sdmmc_slot_config_t *slot_config);

#define SDMMC_HOST_DEFAULT()                                                   \
  ((sdmmc_host_t){.flags = SDMMC_HOST_FLAG_DEINIT_ARG,                        \
                  .slot = 1,                                                   \
                  .init = host_sdmmc_init,                                     \
                  .deinit_p = host_sdmmc_deinit_slot})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})

#endif
