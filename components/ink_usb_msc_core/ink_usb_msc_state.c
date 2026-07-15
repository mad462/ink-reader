#include "ink_usb_msc_state.h"

void ink_usb_msc_state_reset(ink_usb_msc_state_machine_t *machine) {
  if (machine) machine->state = INK_USB_MSC_STOPPED;
}

void ink_usb_msc_state_start_begin(ink_usb_msc_state_machine_t *machine) {
  if (machine) machine->state = INK_USB_MSC_STARTING;
}

void ink_usb_msc_state_start_finish(ink_usb_msc_state_machine_t *machine,
                                    bool succeeded) {
  if (machine)
    machine->state = succeeded ? INK_USB_MSC_ACTIVE : INK_USB_MSC_ERROR;
}

void ink_usb_msc_state_stop_begin(ink_usb_msc_state_machine_t *machine) {
  if (machine) machine->state = INK_USB_MSC_STOPPING;
}

void ink_usb_msc_state_stop_finish(ink_usb_msc_state_machine_t *machine,
                                   bool succeeded) {
  if (machine)
    machine->state = succeeded ? INK_USB_MSC_STOPPED : INK_USB_MSC_ERROR;
}

bool ink_usb_msc_state_can_return_launcher(
    const ink_usb_msc_state_machine_t *machine) {
  return machine && machine->state == INK_USB_MSC_STOPPED;
}
