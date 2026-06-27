#include "tilt_wifi_setup.h"

#include "unity.h"

void test_wifi_setup_selects_strongest_scanned_ap(void)
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

    tilt_wifi_setup_select_strongest(&state);

    TEST_ASSERT_EQUAL(1, state.selected_index);
    TEST_ASSERT_EQUAL_STRING("strong", tilt_wifi_setup_selected_ap(&state)->ssid);
}

void test_wifi_setup_sorts_saved_networks_before_unsaved_then_by_rssi(void)
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

    tilt_wifi_setup_sort_scan(&state);

    TEST_ASSERT_EQUAL_STRING("saved-strong", state.scan.results[0].ssid);
    TEST_ASSERT_EQUAL_STRING("saved-weak", state.scan.results[1].ssid);
    TEST_ASSERT_EQUAL_STRING("open-strong", state.scan.results[2].ssid);
    TEST_ASSERT_EQUAL_STRING("open-weak", state.scan.results[3].ssid);
    TEST_ASSERT_EQUAL(0, state.selected_index);
}

void test_wifi_setup_sorts_connected_network_before_other_saved_networks(void)
{
    wifi_setup_state_t state = {
        .selected_index = 0,
        .status = {
            .connected = true,
            .ssid = "saved-weak",
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

    tilt_wifi_setup_sort_scan(&state);

    TEST_ASSERT_EQUAL_STRING("saved-weak", state.scan.results[0].ssid);
    TEST_ASSERT_EQUAL_STRING("saved-strong", state.scan.results[1].ssid);
    TEST_ASSERT_EQUAL_STRING("open-strong", state.scan.results[2].ssid);
}

void test_wifi_setup_opens_saved_menu_only_for_saved_networks(void)
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

    tilt_wifi_setup_open_saved_menu(&state);
    TEST_ASSERT_EQUAL(WIFI_SETUP_UI_SAVED_MENU, state.mode);
    TEST_ASSERT_EQUAL(0, state.menu_index);

    state.mode = WIFI_SETUP_UI_LIST;
    state.selected_index = 1;
    tilt_wifi_setup_open_saved_menu(&state);
    TEST_ASSERT_EQUAL(WIFI_SETUP_UI_LIST, state.mode);
}

void test_wifi_setup_cycles_selection_with_wraparound(void)
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

    tilt_wifi_setup_cycle_selection(&state, -1);
    TEST_ASSERT_EQUAL(3, state.selected_index);
    TEST_ASSERT_TRUE(tilt_wifi_setup_is_scan_selected(&state));

    tilt_wifi_setup_cycle_selection(&state, 1);
    TEST_ASSERT_EQUAL(0, state.selected_index);
}

void test_wifi_setup_scan_card_is_extra_selectable_item_after_aps(void)
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

    TEST_ASSERT_EQUAL(3, tilt_wifi_setup_selectable_count(&state));

    tilt_wifi_setup_cycle_selection(&state, 1);
    TEST_ASSERT_EQUAL(2, state.selected_index);
    TEST_ASSERT_TRUE(tilt_wifi_setup_is_scan_selected(&state));
    TEST_ASSERT_NULL(tilt_wifi_setup_selected_ap(&state));

    tilt_wifi_setup_open_password(&state);
    TEST_ASSERT_EQUAL(WIFI_SETUP_UI_LIST, state.mode);
}

void test_wifi_setup_scan_card_is_available_when_scan_empty(void)
{
    wifi_setup_state_t state = {
        .selected_index = -1,
        .mode = WIFI_SETUP_UI_LIST,
    };

    tilt_wifi_setup_select_strongest(&state);

    TEST_ASSERT_EQUAL(1, tilt_wifi_setup_selectable_count(&state));
    TEST_ASSERT_EQUAL(0, state.selected_index);
    TEST_ASSERT_TRUE(tilt_wifi_setup_is_scan_selected(&state));
    TEST_ASSERT_NULL(tilt_wifi_setup_selected_ap(&state));
}

void test_wifi_setup_state_transitions_for_password_cancel_and_result_confirm(void)
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

    tilt_wifi_setup_open_password(&state);
    TEST_ASSERT_EQUAL(WIFI_SETUP_UI_PASSWORD, state.mode);

    tilt_wifi_setup_cancel_popup(&state);
    TEST_ASSERT_EQUAL(WIFI_SETUP_UI_LIST, state.mode);

    tilt_wifi_setup_open_password(&state);
    tilt_wifi_setup_begin_connecting(&state);
    TEST_ASSERT_EQUAL(WIFI_SETUP_UI_CONNECTING, state.mode);

    tilt_wifi_setup_finish_connecting(&state, ESP_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL(WIFI_SETUP_UI_RESULT, state.mode);
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, state.result_error);
    TEST_ASSERT_FALSE(state.result_confirmed);

    tilt_wifi_setup_confirm_result(&state);
    TEST_ASSERT_EQUAL(WIFI_SETUP_UI_LIST, state.mode);
    TEST_ASSERT_TRUE(state.result_confirmed);
}

void test_wifi_setup_selected_ap_is_null_when_scan_empty(void)
{
    wifi_setup_state_t state = {
        .selected_index = -1,
    };

    TEST_ASSERT_NULL(tilt_wifi_setup_selected_ap(&state));
}

void test_wifi_scan_add_or_merge_keeps_strongest_duplicate_ssid_and_counts_aps(void)
{
    ink_wifi_scan_list_t scan = {0};

    TEST_ASSERT_TRUE(ink_wifi_scan_list_add_or_merge(
        &scan,
        &(const ink_wifi_scan_result_t){
            .ssid = "sifei_wifi_3",
            .rssi = -72,
            .authmode = 3,
            .saved = true,
            .ap_count = 1,
        }));
    TEST_ASSERT_TRUE(ink_wifi_scan_list_add_or_merge(
        &scan,
        &(const ink_wifi_scan_result_t){
            .ssid = "sifei_wifi_3",
            .rssi = -48,
            .authmode = 4,
            .saved = true,
            .ap_count = 1,
        }));

    TEST_ASSERT_EQUAL(1, scan.count);
    TEST_ASSERT_EQUAL_STRING("sifei_wifi_3", scan.results[0].ssid);
    TEST_ASSERT_EQUAL_INT8(-48, scan.results[0].rssi);
    TEST_ASSERT_EQUAL_UINT8(2, scan.results[0].ap_count);
    TEST_ASSERT_EQUAL_UINT8(4, scan.results[0].authmode);
    TEST_ASSERT_TRUE(scan.results[0].saved);
}
