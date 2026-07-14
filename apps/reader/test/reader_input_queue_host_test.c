#include <stdint.h>
#include <stdio.h>

#include "reader_input_queue.h"

int main(void) {
  reader_input_queue_t queue;
  reader_input_queue_init(&queue);

  for (uint32_t tap = 0; tap < 4U; ++tap) {
    const reader_input_event_t pressed = {
        .down = 1U,
        .pressed = 1U,
        .held_ms = {tap * 10U},
    };
    const reader_input_event_t released = {
        .released = 1U,
        .held_ms = {tap * 10U + 5U},
    };
    if (!reader_input_queue_push(&queue, &pressed) ||
        !reader_input_queue_push(&queue, &released)) {
      fputs("queue dropped a four-tap burst\n", stderr);
      return 1;
    }
  }

  for (uint32_t tap = 0; tap < 4U; ++tap) {
    reader_input_event_t event = {0};
    if (!reader_input_queue_pop(&queue, &event) || event.pressed != 1U ||
        event.released != 0U || event.down != 1U ||
        event.held_ms[0] != tap * 10U) {
      fputs("pressed edge replay order failed\n", stderr);
      return 1;
    }
    if (!reader_input_queue_pop(&queue, &event) || event.pressed != 0U ||
        event.released != 1U || event.down != 0U ||
        event.held_ms[0] != tap * 10U + 5U) {
      fputs("released edge replay order failed\n", stderr);
      return 1;
    }
  }

  reader_input_event_t event = {0};
  if (reader_input_queue_pop(&queue, &event)) {
    fputs("empty queue returned an event\n", stderr);
    return 1;
  }

  puts("PASS: reader deferred input queue host tests");
  return 0;
}
