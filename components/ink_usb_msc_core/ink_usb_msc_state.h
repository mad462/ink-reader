#pragma once

#include <stdbool.h>

#include "ink_usb_msc_core.h"

typedef struct {
  ink_usb_msc_state_t state;
} ink_usb_msc_state_machine_t;

void ink_usb_msc_state_reset(ink_usb_msc_state_machine_t *machine);
void ink_usb_msc_state_start_begin(ink_usb_msc_state_machine_t *machine);
void ink_usb_msc_state_start_finish(ink_usb_msc_state_machine_t *machine,
                                    bool succeeded);
void ink_usb_msc_state_stop_begin(ink_usb_msc_state_machine_t *machine);
void ink_usb_msc_state_stop_finish(ink_usb_msc_state_machine_t *machine,
                                   bool succeeded);
bool ink_usb_msc_state_can_return_launcher(
    const ink_usb_msc_state_machine_t *machine);
