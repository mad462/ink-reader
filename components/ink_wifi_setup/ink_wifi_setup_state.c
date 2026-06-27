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

static bool test_selects_strongest_scanned_ap(void);
static bool test_sorts_saved_before_unsaved_then_by_rssi(void);
static bool test_sorts_connected_before_other_saved(void);
static bool test_opens_saved_menu_only_for_saved_networks(void);
static bool test_cycles_selection_with_wraparound(void);
static bool test_scan_card_is_extra_selectable_item_after_aps(void);
static bool test_scan_card_is_available_when_scan_empty(void);
static bool test_state_transitions_for_password_cancel_and_result_confirm(void);
static bool test_selected_ap_is_null_when_scan_empty(void);
static bool test_scan_add_or_merge_keeps_strongest_duplicate(void);

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

static bool test_selects_strongest_scanned_ap(void)
{
    wifi_setup_state_t state = {
        .selected_index = -1,
        .scan = {
            .count = 3,
            .results = {
                {.ssid = "weak", .rssi = -78},
                {.ssid = "strong", .rssi = -35},
                {.ssid = "middle", .rssi = -55},
            },
        },
    };
    const ink_wifi_scan_result_t *selected = NULL;

    ink_wifi_setup_select_strongest(&state);
    selected = ink_wifi_setup_selected_ap(&state);

    return state.selected_index == 1
        && selected != NULL
        && strcmp(selected->ssid, "strong") == 0;
}

static bool test_sorts_saved_before_unsaved_then_by_rssi(void)
{
    wifi_setup_state_t state = {
        .selected_index = 0,
        .scan = {
            .count = 4,
            .results = {
                {.ssid = "open-strong", .rssi = -25, .saved = false},
                {.ssid = "saved-weak", .rssi = -80, .saved = true},
                {.ssid = "saved-strong", .rssi = -45, .saved = true},
                {.ssid = "open-weak", .rssi = -70, .saved = false},
            },
        },
    };

    ink_wifi_setup_sort_scan(&state);

    return strcmp(state.scan.results[0].ssid, "saved-strong") == 0
        && strcmp(state.scan.results[1].ssid, "saved-weak") == 0
        && strcmp(state.scan.results[2].ssid, "open-strong") == 0
        && strcmp(state.scan.results[3].ssid, "open-weak") == 0
        && state.selected_index == 0;
}

static bool test_sorts_connected_before_other_saved(void)
{
    wifi_setup_state_t state = {
        .selected_index = 0,
        .status = {
            .connected = true,
        },
        .scan = {
            .count = 3,
            .results = {
                {.ssid = "saved-strong", .rssi = -45, .saved = true},
                {.ssid = "saved-weak", .rssi = -80, .saved = true},
                {.ssid = "open-strong", .rssi = -25, .saved = false},
            },
        },
    };

    strcpy(state.status.ssid, "saved-weak");
    ink_wifi_setup_sort_scan(&state);

    return strcmp(state.scan.results[0].ssid, "saved-weak") == 0
        && strcmp(state.scan.results[1].ssid, "saved-strong") == 0
        && strcmp(state.scan.results[2].ssid, "open-strong") == 0;
}

static bool test_opens_saved_menu_only_for_saved_networks(void)
{
    wifi_setup_state_t state = {
        .selected_index = 0,
        .mode = WIFI_SETUP_UI_LIST,
        .scan = {
            .count = 2,
            .results = {
                {.ssid = "saved", .rssi = -50, .saved = true},
                {.ssid = "new", .rssi = -40, .saved = false},
            },
        },
    };

    ink_wifi_setup_open_saved_menu(&state);
    if (state.mode != WIFI_SETUP_UI_SAVED_MENU || state.menu_index != 0) {
        return false;
    }

    state.mode = WIFI_SETUP_UI_LIST;
    state.selected_index = 1;
    ink_wifi_setup_open_saved_menu(&state);
    return state.mode == WIFI_SETUP_UI_LIST;
}

static bool test_cycles_selection_with_wraparound(void)
{
    wifi_setup_state_t state = {
        .selected_index = 0,
        .scan = {
            .count = 3,
            .results = {
                {.ssid = "one", .rssi = -50},
                {.ssid = "two", .rssi = -40},
                {.ssid = "three", .rssi = -30},
            },
        },
    };

    ink_wifi_setup_cycle_selection(&state, -1);
    if (state.selected_index != 3 || !ink_wifi_setup_is_scan_selected(&state)) {
        return false;
    }

    ink_wifi_setup_cycle_selection(&state, 1);
    return state.selected_index == 0;
}

static bool test_scan_card_is_extra_selectable_item_after_aps(void)
{
    wifi_setup_state_t state = {
        .selected_index = 1,
        .scan = {
            .count = 2,
            .results = {
                {.ssid = "one", .rssi = -50},
                {.ssid = "two", .rssi = -40},
            },
        },
    };

    if (ink_wifi_setup_selectable_count(&state) != 3) {
        return false;
    }

    ink_wifi_setup_cycle_selection(&state, 1);
    if (state.selected_index != 2
        || !ink_wifi_setup_is_scan_selected(&state)
        || ink_wifi_setup_selected_ap(&state) != NULL) {
        return false;
    }

    ink_wifi_setup_open_password(&state);
    return state.mode == WIFI_SETUP_UI_LIST;
}

static bool test_scan_card_is_available_when_scan_empty(void)
{
    wifi_setup_state_t state = {
        .selected_index = -1,
        .mode = WIFI_SETUP_UI_LIST,
    };

    ink_wifi_setup_select_strongest(&state);

    return ink_wifi_setup_selectable_count(&state) == 1
        && state.selected_index == 0
        && ink_wifi_setup_is_scan_selected(&state)
        && ink_wifi_setup_selected_ap(&state) == NULL;
}

static bool test_state_transitions_for_password_cancel_and_result_confirm(void)
{
    wifi_setup_state_t state = {
        .selected_index = 0,
        .mode = WIFI_SETUP_UI_LIST,
        .scan = {
            .count = 1,
            .results = {
                {.ssid = "target", .rssi = -42},
            },
        },
    };

    ink_wifi_setup_open_password(&state);
    if (state.mode != WIFI_SETUP_UI_PASSWORD) {
        return false;
    }

    ink_wifi_setup_cancel_popup(&state);
    if (state.mode != WIFI_SETUP_UI_LIST) {
        return false;
    }

    ink_wifi_setup_open_password(&state);
    ink_wifi_setup_begin_connecting(&state);
    if (state.mode != WIFI_SETUP_UI_CONNECTING) {
        return false;
    }

    ink_wifi_setup_finish_connecting(&state, ESP_ERR_TIMEOUT);
    if (state.mode != WIFI_SETUP_UI_RESULT
        || state.result_error != ESP_ERR_TIMEOUT
        || state.result_confirmed) {
        return false;
    }

    ink_wifi_setup_confirm_result(&state);
    return state.mode == WIFI_SETUP_UI_LIST
        && state.result_confirmed;
}

static bool test_selected_ap_is_null_when_scan_empty(void)
{
    wifi_setup_state_t state = {
        .selected_index = -1,
    };

    return ink_wifi_setup_selected_ap(&state) == NULL;
}

static bool test_scan_add_or_merge_keeps_strongest_duplicate(void)
{
    ink_wifi_scan_list_t scan = {0};

    if (!ink_wifi_scan_list_add_or_merge(
            &scan,
            &(const ink_wifi_scan_result_t){
                .ssid = "sifei_wifi_3",
                .rssi = -72,
                .authmode = 3,
                .saved = true,
                .ap_count = 1,
            })) {
        return false;
    }
    if (!ink_wifi_scan_list_add_or_merge(
            &scan,
            &(const ink_wifi_scan_result_t){
                .ssid = "sifei_wifi_3",
                .rssi = -48,
                .authmode = 4,
                .saved = true,
                .ap_count = 1,
            })) {
        return false;
    }

    return scan.count == 1
        && strcmp(scan.results[0].ssid, "sifei_wifi_3") == 0
        && scan.results[0].rssi == -48
        && scan.results[0].ap_count == 2
        && scan.results[0].authmode == 4
        && scan.results[0].saved;
}

bool ink_wifi_setup_state_self_test(void)
{
    return test_selects_strongest_scanned_ap()
        && test_sorts_saved_before_unsaved_then_by_rssi()
        && test_sorts_connected_before_other_saved()
        && test_opens_saved_menu_only_for_saved_networks()
        && test_cycles_selection_with_wraparound()
        && test_scan_card_is_extra_selectable_item_after_aps()
        && test_scan_card_is_available_when_scan_empty()
        && test_state_transitions_for_password_cancel_and_result_confirm()
        && test_selected_ap_is_null_when_scan_empty()
        && test_scan_add_or_merge_keeps_strongest_duplicate();
}
