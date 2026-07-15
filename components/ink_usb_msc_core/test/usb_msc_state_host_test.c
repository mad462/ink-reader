#include "ink_usb_msc_state.h"

#include <stdio.h>

static int expect(bool condition, const char *message) {
  if (condition) return 0;
  fprintf(stderr, "%s\n", message);
  return 1;
}

int main(void) {
  ink_usb_msc_state_machine_t machine;
  ink_usb_msc_state_reset(&machine);
  if (expect(machine.state == INK_USB_MSC_STOPPED,
             "reset must be stopped") ||
      expect(ink_usb_msc_state_can_return_launcher(&machine),
             "stopped must allow return"))
    return 1;

  ink_usb_msc_state_start_begin(&machine);
  if (expect(machine.state == INK_USB_MSC_STARTING,
             "start begin must be starting") ||
      expect(!ink_usb_msc_state_can_return_launcher(&machine),
             "starting must block return"))
    return 1;
  ink_usb_msc_state_start_finish(&machine, true);
  if (expect(machine.state == INK_USB_MSC_ACTIVE,
             "successful start must be active"))
    return 1;

  ink_usb_msc_state_stop_begin(&machine);
  if (expect(machine.state == INK_USB_MSC_STOPPING,
             "stop begin must be stopping"))
    return 1;
  ink_usb_msc_state_stop_finish(&machine, false);
  if (expect(machine.state == INK_USB_MSC_ERROR,
             "failed stop must be error") ||
      expect(!ink_usb_msc_state_can_return_launcher(&machine),
             "failed stop must block return"))
    return 1;

  ink_usb_msc_state_stop_begin(&machine);
  ink_usb_msc_state_stop_finish(&machine, true);
  if (expect(machine.state == INK_USB_MSC_STOPPED,
             "successful retry must be stopped") ||
      expect(ink_usb_msc_state_can_return_launcher(&machine),
             "successful retry must allow return"))
    return 1;

  ink_usb_msc_state_start_begin(&machine);
  ink_usb_msc_state_start_finish(&machine, false);
  if (expect(machine.state == INK_USB_MSC_ERROR,
             "failed start must be error") ||
      expect(!ink_usb_msc_state_can_return_launcher(&machine),
             "failed start with cleanup pending must block return"))
    return 1;

  puts("PASS: USB MSC state host test");
  return 0;
}
