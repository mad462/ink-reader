#include "ink_input.h"

#include <string.h>

#include "driver/gpio.h"

#define DEBOUNCE_MS 20u

typedef struct {
  bool initialized;
  uint32_t raw;
  uint32_t stable;
  uint32_t previous;
  uint32_t changed_at;
  uint32_t pressed_at[INK_BUTTON_COUNT];
} input_state_t;
static input_state_t s_state;
static const gpio_num_t kPins[INK_BUTTON_COUNT] = {
    GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_12, GPIO_NUM_11, GPIO_NUM_46};

uint32_t ink_input_mask(ink_button_t button) {
  return button >= 0 && button < INK_BUTTON_COUNT ? 1u << button : 0;
}
bool ink_input_is_down(const ink_input_snapshot_t *s, ink_button_t b) {
  return s && (s->down & ink_input_mask(b));
}
bool ink_input_is_raw_down(const ink_input_snapshot_t *s, ink_button_t b) {
  return s && (s->raw_down & ink_input_mask(b));
}
bool ink_input_was_pressed(const ink_input_snapshot_t *s, ink_button_t b) {
  return s && (s->pressed & ink_input_mask(b));
}
uint32_t ink_input_held_ms(const ink_input_snapshot_t *s, ink_button_t b) {
  return s && b >= 0 && b < INK_BUTTON_COUNT ? s->held_ms[b] : 0;
}

static esp_err_t step(input_state_t *state, uint32_t raw, uint32_t now,
                      ink_input_snapshot_t *out) {
  if (!state || !out) return ESP_ERR_INVALID_ARG;
  if (raw != state->raw) {
    state->raw = raw;
    state->changed_at = now;
  }
  if (state->stable != state->raw && now - state->changed_at >= DEBOUNCE_MS)
    state->stable = state->raw;
  memset(out, 0, sizeof(*out));
  out->raw_down = state->raw;
  out->down = state->stable;
  out->pressed = state->stable & ~state->previous;
  out->released = state->previous & ~state->stable;
  for (int i = 0; i < INK_BUTTON_COUNT; ++i) {
    const uint32_t mask = 1u << i;
    if (out->pressed & mask) state->pressed_at[i] = now;
    if (out->released & mask) state->pressed_at[i] = 0;
    if (out->down & mask) out->held_ms[i] = now - state->pressed_at[i];
  }
  state->previous = state->stable;
  return ESP_OK;
}

esp_err_t ink_input_init(void) {
  uint64_t mask = 0;
  memset(&s_state, 0, sizeof(s_state));
  for (int i = 0; i < INK_BUTTON_COUNT; ++i) mask |= 1ULL << kPins[i];
  gpio_config_t cfg = {.pin_bit_mask = mask,
                       .mode = GPIO_MODE_INPUT,
                       .pull_up_en = GPIO_PULLUP_ENABLE,
                       .pull_down_en = GPIO_PULLDOWN_DISABLE,
                       .intr_type = GPIO_INTR_DISABLE};
  esp_err_t ret = gpio_config(&cfg);
  if (ret == ESP_OK) s_state.initialized = true;
  return ret;
}

esp_err_t ink_input_poll(uint32_t now_ms, ink_input_snapshot_t *snapshot) {
  if (!s_state.initialized || !snapshot) return ESP_ERR_INVALID_STATE;
  uint32_t raw = 0;
  for (int i = 0; i < INK_BUTTON_COUNT; ++i)
    if (gpio_get_level(kPins[i]) == 0) raw |= 1u << i;
  return step(&s_state, raw, now_ms, snapshot);
}

bool ink_input_self_test(void) {
  input_state_t state = {0};
  ink_input_snapshot_t out;
  const uint32_t back = ink_input_mask(INK_BUTTON_BACK);
  if (step(&state, back, 1, &out) != ESP_OK || out.pressed) return false;
  if (step(&state, back, 22, &out) != ESP_OK ||
      !ink_input_was_pressed(&out, INK_BUTTON_BACK))
    return false;
  if (step(&state, back, 1222, &out) != ESP_OK ||
      ink_input_held_ms(&out, INK_BUTTON_BACK) != 1200)
    return false;
  if (step(&state, 0, 1223, &out) != ESP_OK || out.released ||
      ink_input_is_raw_down(&out, INK_BUTTON_BACK) ||
      !ink_input_is_down(&out, INK_BUTTON_BACK))
    return false;
  return step(&state, 0, 1244, &out) == ESP_OK && (out.released & back);
}
