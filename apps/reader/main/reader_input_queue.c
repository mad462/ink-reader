#include "reader_input_queue.h"

#include <string.h>

void reader_input_queue_init(reader_input_queue_t *queue) {
  if (queue) memset(queue, 0, sizeof(*queue));
}

bool reader_input_queue_push(reader_input_queue_t *queue,
                             const reader_input_event_t *event) {
  if (!queue || !event || queue->count >= READER_INPUT_QUEUE_CAPACITY)
    return false;
  const size_t tail =
      (queue->head + queue->count) % READER_INPUT_QUEUE_CAPACITY;
  queue->events[tail] = *event;
  ++queue->count;
  return true;
}

bool reader_input_queue_pop(reader_input_queue_t *queue,
                            reader_input_event_t *event) {
  if (!queue || !event || queue->count == 0U) return false;
  *event = queue->events[queue->head];
  queue->head = (queue->head + 1U) % READER_INPUT_QUEUE_CAPACITY;
  --queue->count;
  return true;
}
