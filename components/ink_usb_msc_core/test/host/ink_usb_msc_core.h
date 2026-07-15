#ifndef INK_USB_MSC_CORE_HOST_H
#define INK_USB_MSC_CORE_HOST_H

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
  INK_USB_MSC_STOPPED = 0,
  INK_USB_MSC_STARTING,
  INK_USB_MSC_ACTIVE,
  INK_USB_MSC_STOPPING,
  INK_USB_MSC_ERROR,
} ink_usb_msc_state_t;

esp_err_t ink_usb_msc_core_start(void);
esp_err_t ink_usb_msc_core_stop(void);
ink_usb_msc_state_t ink_usb_msc_core_state(void);
bool ink_usb_msc_core_can_return_launcher(void);

#endif
