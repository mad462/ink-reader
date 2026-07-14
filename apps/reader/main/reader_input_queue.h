#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  READER_INPUT_HELD_COUNT = 5,
  READER_INPUT_QUEUE_CAPACITY = 16,
};

typedef struct {
  uint32_t raw_down;
  uint32_t down;
  uint32_t pressed;
  uint32_t released;
  uint32_t held_ms[READER_INPUT_HELD_COUNT];
} reader_input_event_t;

typedef struct {
  reader_input_event_t events[READER_INPUT_QUEUE_CAPACITY];
  size_t head;
  size_t count;
} reader_input_queue_t;

void reader_input_queue_init(reader_input_queue_t *queue);
bool reader_input_queue_push(reader_input_queue_t *queue,
                             const reader_input_event_t *event);
bool reader_input_queue_pop(reader_input_queue_t *queue,
                            reader_input_event_t *event);
