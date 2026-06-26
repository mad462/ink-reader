#pragma once

#include "ink_wifi_manager.h"

typedef enum {
    WIFI_SETUP_UI_LIST = 0,
    WIFI_SETUP_UI_SAVED_MENU,
    WIFI_SETUP_UI_PASSWORD,
    WIFI_SETUP_UI_CONNECTING,
    WIFI_SETUP_UI_RESULT,
} wifi_setup_ui_mode_t;

typedef struct {
    ink_wifi_scan_list_t scan;
    int selected_index;
    int menu_index;
    ink_wifi_status_t status;
    wifi_setup_ui_mode_t mode;
    esp_err_t result_error;
    bool result_confirmed;
    bool scan_in_progress;
} wifi_setup_state_t;

int ink_wifi_setup_selectable_count(const wifi_setup_state_t *state);
bool ink_wifi_setup_is_scan_selected(const wifi_setup_state_t *state);
void ink_wifi_setup_select_strongest(wifi_setup_state_t *state);
void ink_wifi_setup_sort_scan(wifi_setup_state_t *state);
void ink_wifi_setup_cycle_selection(wifi_setup_state_t *state, int direction);
const ink_wifi_scan_result_t *ink_wifi_setup_selected_ap(const wifi_setup_state_t *state);
void ink_wifi_setup_open_password(wifi_setup_state_t *state);
void ink_wifi_setup_open_saved_menu(wifi_setup_state_t *state);
void ink_wifi_setup_cancel_popup(wifi_setup_state_t *state);
void ink_wifi_setup_begin_connecting(wifi_setup_state_t *state);
void ink_wifi_setup_finish_connecting(wifi_setup_state_t *state, esp_err_t result);
void ink_wifi_setup_confirm_result(wifi_setup_state_t *state);
