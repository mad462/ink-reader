#ifndef SDMMC_CMD_HOST_H
#define SDMMC_CMD_HOST_H

#include "driver/sdmmc_host.h"

typedef struct {
  int unused;
} sdmmc_card_t;

esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *card);

#endif
