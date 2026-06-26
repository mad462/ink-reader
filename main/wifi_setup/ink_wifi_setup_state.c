#include "ink_wifi_setup_state.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool scan_entry_should_sort_before(
    const ink_wifi_scan_result_t *left,
    const ink_wifi_scan_result_t *right,
    const wifi_setup_state_t *state)
{
    const bool left_connected =
        state != NULL && state->status.connected && strcmp(state->status.ssid, left->ssid) == 0;
    const bool right_connected =
        state != NULL && state->status.connected && strcmp(state->status.ssid, right->ssid) == 0;
    if (left_connected != right_connected) {
        return left_connected;
    }
    if (left->saved != right->saved) {
        return left->saved;
    }
    return left->rssi > right->rssi;
}

int ink_wifi_setup_selectable_count(const wifi_setup_state_t *state)
{
    if (state == NULL) {
        return 0;
    }
    return (int)state->scan.count + 1;
}

bool ink_wifi_setup_is_scan_selected(const wifi_setup_state_t *state)
{
    if (state == NULL) {
        return false;
    }
    return state->selected_index == (int)state->scan.count;
}

void ink_wifi_setup_select_strongest(wifi_setup_state_t *state)
{
    if (state == NULL) {
        return;
    }

    if (state->scan.count == 0) {
        state->selected_index = 0;
        return;
    }

    int strongest = 0;
    for (uint16_t i = 1; i < state->scan.count; ++i) {
        if (state->scan.results[i].rssi > state->scan.results[strongest].rssi) {
            strongest = (int)i;
        }
    }
    state->selected_index = strongest;
}

void ink_wifi_setup_sort_scan(wifi_setup_state_t *state)
{
    if (state == NULL) {
        return;
    }

    if (state->scan.count == 0) {
        state->selected_index = 0;
        return;
    }

    for (uint16_t i = 1; i < state->scan.count; ++i) {
        ink_wifi_scan_result_t value = state->scan.results[i];
        uint16_t j = i;
        while (j > 0 && scan_entry_should_sort_before(&value, &state->scan.results[j - 1], state)) {
            state->scan.results[j] = state->scan.results[j - 1];
            --j;
        }
        state->scan.results[j] = value;
    }
    state->selected_index = 0;
}

void ink_wifi_setup_cycle_selection(wifi_setup_state_t *state, int direction)
{
    const int count = ink_wifi_setup_selectable_count(state);
    if (state == NULL || count == 0 || direction == 0) {
        return;
    }

    if (state->selected_index < 0 || state->selected_index >= count) {
        ink_wifi_setup_select_strongest(state);
        return;
    }

    state->selected_index = (state->selected_index + (direction > 0 ? 1 : -1) + count) % count;
}

const ink_wifi_scan_result_t *ink_wifi_setup_selected_ap(const wifi_setup_state_t *state)
{
    if (state == NULL || state->selected_index < 0 || state->selected_index >= state->scan.count) {
        return NULL;
    }
    return &state->scan.results[state->selected_index];
}

void ink_wifi_setup_open_password(wifi_setup_state_t *state)
{
    if (ink_wifi_setup_selected_ap(state) == NULL) {
        return;
    }
    state->mode = WIFI_SETUP_UI_PASSWORD;
    state->result_confirmed = false;
}

void ink_wifi_setup_open_saved_menu(wifi_setup_state_t *state)
{
    const ink_wifi_scan_result_t *ap = ink_wifi_setup_selected_ap(state);
    if (state == NULL || ap == NULL || !ap->saved) {
        return;
    }
    state->mode = WIFI_SETUP_UI_SAVED_MENU;
    state->menu_index = 0;
    state->result_confirmed = false;
}

void ink_wifi_setup_cancel_popup(wifi_setup_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->mode = WIFI_SETUP_UI_LIST;
}

void ink_wifi_setup_begin_connecting(wifi_setup_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->mode = WIFI_SETUP_UI_CONNECTING;
    state->result_confirmed = false;
    state->result_error = ESP_ERR_INVALID_STATE;
}

void ink_wifi_setup_finish_connecting(wifi_setup_state_t *state, esp_err_t result)
{
    if (state == NULL) {
        return;
    }
    state->mode = WIFI_SETUP_UI_RESULT;
    state->result_error = result;
    state->result_confirmed = false;
}

void ink_wifi_setup_confirm_result(wifi_setup_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->mode = WIFI_SETUP_UI_LIST;
    state->result_confirmed = true;
}
