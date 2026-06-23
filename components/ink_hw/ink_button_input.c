#include "ink_button_input.h"

#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"

enum {
    INK_BUTTON_DEBOUNCE_MS = 20,
};

typedef struct {
    gpio_num_t gpio_num;
    bool active_low;
} ink_button_gpio_desc_t;

typedef struct {
    bool initialized;
    uint32_t raw_sample_mask;
    uint32_t stable_mask;
    uint32_t previous_stable_mask;
    uint32_t last_transition_ms;
    uint32_t press_started_ms[INK_RAW_BUTTON_COUNT];
} ink_button_state_t;

static const ink_button_gpio_desc_t s_button_gpios[INK_RAW_BUTTON_COUNT] = {
    [INK_RAW_BUTTON_BACK] = {.gpio_num = GPIO_NUM_9, .active_low = true},
    [INK_RAW_BUTTON_CONFIRM] = {.gpio_num = GPIO_NUM_10, .active_low = true},
    [INK_RAW_BUTTON_LEFT] = {.gpio_num = GPIO_NUM_12, .active_low = true},
    [INK_RAW_BUTTON_RIGHT] = {.gpio_num = GPIO_NUM_11, .active_low = true},
    [INK_RAW_BUTTON_POWER] = {.gpio_num = GPIO_NUM_46, .active_low = true},
};

static ink_button_state_t s_button_state;

static uint32_t ink_button_read_raw_mask(void);
static esp_err_t ink_button_step(
    ink_button_state_t *state,
    uint32_t raw_mask,
    uint32_t now_ms,
    ink_button_snapshot_t *snapshot
);

uint32_t ink_button_input_mask_for_raw(ink_raw_button_t button)
{
    if (button < 0 || button >= INK_RAW_BUTTON_COUNT) {
        return 0;
    }
    return 1UL << (uint32_t)button;
}

uint32_t ink_button_input_mask_for_logical(ink_logical_button_t button)
{
    switch (button) {
        case INK_LOGICAL_BUTTON_BACK:
            return ink_button_input_mask_for_raw(INK_RAW_BUTTON_BACK);
        case INK_LOGICAL_BUTTON_CONFIRM:
            return ink_button_input_mask_for_raw(INK_RAW_BUTTON_CONFIRM);
        case INK_LOGICAL_BUTTON_LEFT:
        case INK_LOGICAL_BUTTON_NAV_PREVIOUS:
            return ink_button_input_mask_for_raw(INK_RAW_BUTTON_LEFT);
        case INK_LOGICAL_BUTTON_RIGHT:
        case INK_LOGICAL_BUTTON_NAV_NEXT:
            return ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
        case INK_LOGICAL_BUTTON_POWER:
            return ink_button_input_mask_for_raw(INK_RAW_BUTTON_POWER);
        default:
            return 0;
    }
}

bool ink_button_snapshot_is_pressed(const ink_button_snapshot_t *snapshot, ink_logical_button_t button)
{
    if (snapshot == NULL) {
        return false;
    }
    return (snapshot->stable_mask & ink_button_input_mask_for_logical(button)) != 0;
}

bool ink_button_snapshot_was_pressed(const ink_button_snapshot_t *snapshot, ink_logical_button_t button)
{
    if (snapshot == NULL) {
        return false;
    }
    return (snapshot->pressed_mask & ink_button_input_mask_for_logical(button)) != 0;
}

bool ink_button_snapshot_was_released(const ink_button_snapshot_t *snapshot, ink_logical_button_t button)
{
    if (snapshot == NULL) {
        return false;
    }
    return (snapshot->released_mask & ink_button_input_mask_for_logical(button)) != 0;
}

uint32_t ink_button_snapshot_get_held_ms(const ink_button_snapshot_t *snapshot, ink_logical_button_t button)
{
    uint32_t mask;

    if (snapshot == NULL) {
        return 0;
    }

    mask = ink_button_input_mask_for_logical(button);
    for (int i = 0; i < INK_RAW_BUTTON_COUNT; ++i) {
        if (mask == (1UL << i)) {
            return snapshot->held_duration_ms[i];
        }
    }

    return 0;
}

esp_err_t ink_button_input_init(void)
{
    uint64_t pin_mask = 0;
    gpio_config_t cfg = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    memset(&s_button_state, 0, sizeof(s_button_state));

    for (int i = 0; i < INK_RAW_BUTTON_COUNT; ++i) {
        pin_mask |= 1ULL << (uint32_t)s_button_gpios[i].gpio_num;
    }
    cfg.pin_bit_mask = pin_mask;

    ESP_RETURN_ON_ERROR(gpio_config(&cfg), "ink_button_input", "button gpio config failed");
    s_button_state.raw_sample_mask = ink_button_read_raw_mask();
    s_button_state.stable_mask = s_button_state.raw_sample_mask;
    s_button_state.previous_stable_mask = s_button_state.raw_sample_mask;
    s_button_state.initialized = true;
    return ESP_OK;
}

esp_err_t ink_button_input_poll(ink_button_snapshot_t *snapshot, uint32_t now_ms)
{
    if (!s_button_state.initialized || snapshot == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return ink_button_step(&s_button_state, ink_button_read_raw_mask(), now_ms, snapshot);
}

static uint32_t ink_button_read_raw_mask(void)
{
    uint32_t mask = 0;

    for (int i = 0; i < INK_RAW_BUTTON_COUNT; ++i) {
        const int level = gpio_get_level(s_button_gpios[i].gpio_num);
        const bool pressed = s_button_gpios[i].active_low ? (level == 0) : (level != 0);
        if (pressed) {
            mask |= ink_button_input_mask_for_raw((ink_raw_button_t)i);
        }
    }

    return mask;
}

static esp_err_t ink_button_step(
    ink_button_state_t *state,
    uint32_t raw_mask,
    uint32_t now_ms,
    ink_button_snapshot_t *snapshot)
{
    uint32_t changed_mask;

    if (state == NULL || snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (raw_mask != state->raw_sample_mask) {
        state->raw_sample_mask = raw_mask;
        state->last_transition_ms = now_ms;
    }

    if (state->stable_mask != state->raw_sample_mask
        && (uint32_t)(now_ms - state->last_transition_ms) >= INK_BUTTON_DEBOUNCE_MS) {
        state->stable_mask = state->raw_sample_mask;
    }

    snapshot->stable_mask = state->stable_mask;
    snapshot->pressed_mask = state->stable_mask & ~state->previous_stable_mask;
    snapshot->released_mask = state->previous_stable_mask & ~state->stable_mask;

    changed_mask = snapshot->pressed_mask | snapshot->released_mask;
    for (int i = 0; i < INK_RAW_BUTTON_COUNT; ++i) {
        const uint32_t mask = 1UL << i;
        if (snapshot->pressed_mask & mask) {
            state->press_started_ms[i] = now_ms;
        } else if (snapshot->released_mask & mask) {
            state->press_started_ms[i] = 0;
        }

        if (snapshot->stable_mask & mask) {
            snapshot->held_duration_ms[i] = now_ms - state->press_started_ms[i];
        } else if (changed_mask & mask) {
            snapshot->held_duration_ms[i] = 0;
        } else {
            snapshot->held_duration_ms[i] = 0;
        }
    }

    state->previous_stable_mask = state->stable_mask;
    return ESP_OK;
}

bool ink_button_input_self_test(void)
{
    ink_button_state_t state;
    ink_button_snapshot_t snapshot;
    const uint32_t left_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_LEFT);
    const uint32_t right_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_RIGHT);
    const uint32_t confirm_mask = ink_button_input_mask_for_raw(INK_RAW_BUTTON_CONFIRM);

    memset(&state, 0, sizeof(state));
    memset(&snapshot, 0, sizeof(snapshot));

    if (ink_button_step(&state, 0, 0, &snapshot) != ESP_OK) {
        return false;
    }
    if (snapshot.stable_mask != 0 || snapshot.pressed_mask != 0 || snapshot.released_mask != 0) {
        return false;
    }

    if (ink_button_step(&state, left_mask, 5, &snapshot) != ESP_OK) {
        return false;
    }
    if (snapshot.stable_mask != 0 || snapshot.pressed_mask != 0) {
        return false;
    }

    if (ink_button_step(&state, left_mask, 26, &snapshot) != ESP_OK) {
        return false;
    }
    if (!ink_button_snapshot_was_pressed(&snapshot, INK_LOGICAL_BUTTON_LEFT)) {
        return false;
    }
    if (!ink_button_snapshot_was_pressed(&snapshot, INK_LOGICAL_BUTTON_NAV_PREVIOUS)) {
        return false;
    }
    if (ink_button_snapshot_was_pressed(&snapshot, INK_LOGICAL_BUTTON_NAV_NEXT)) {
        return false;
    }

    if (ink_button_step(&state, left_mask, 60, &snapshot) != ESP_OK) {
        return false;
    }
    if (!ink_button_snapshot_is_pressed(&snapshot, INK_LOGICAL_BUTTON_LEFT)) {
        return false;
    }
    if (ink_button_snapshot_get_held_ms(&snapshot, INK_LOGICAL_BUTTON_LEFT) < 34) {
        return false;
    }

    if (ink_button_step(&state, 0, 90, &snapshot) != ESP_OK) {
        return false;
    }
    if (snapshot.released_mask != 0) {
        return false;
    }

    if (ink_button_step(&state, 0, 111, &snapshot) != ESP_OK) {
        return false;
    }
    if (!ink_button_snapshot_was_released(&snapshot, INK_LOGICAL_BUTTON_LEFT)) {
        return false;
    }
    if (!ink_button_snapshot_was_released(&snapshot, INK_LOGICAL_BUTTON_NAV_PREVIOUS)) {
        return false;
    }

    if (ink_button_step(&state, confirm_mask | right_mask, 140, &snapshot) != ESP_OK) {
        return false;
    }
    if (ink_button_step(&state, confirm_mask | right_mask, 165, &snapshot) != ESP_OK) {
        return false;
    }
    if (!ink_button_snapshot_was_pressed(&snapshot, INK_LOGICAL_BUTTON_CONFIRM)) {
        return false;
    }
    if (!ink_button_snapshot_was_pressed(&snapshot, INK_LOGICAL_BUTTON_RIGHT)) {
        return false;
    }
    if (!ink_button_snapshot_was_pressed(&snapshot, INK_LOGICAL_BUTTON_NAV_NEXT)) {
        return false;
    }
    if (!ink_button_snapshot_is_pressed(&snapshot, INK_LOGICAL_BUTTON_CONFIRM)
        || !ink_button_snapshot_is_pressed(&snapshot, INK_LOGICAL_BUTTON_RIGHT)) {
        return false;
    }

    return true;
}
