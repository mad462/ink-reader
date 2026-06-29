#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "ink_app_iface.h"
#include "ink_cpfont.h"
#include "epd_test_pattern.h"
#include "voice_note/voice_note_types.h"

typedef struct {
    voice_note_tab_t active_tab;
    size_t selected_index;
    bool tab_bar_focus;
    bool popup_open;
    uint8_t popup_action_index;
    bool full_text_open;
    size_t visible_note_count;
    voice_note_note_t *visible_notes;
    epd_test_pattern_reader_menu_overlay_t *popup_overlay;
    voice_note_service_snapshot_t snapshot;
    char active_note_title[VOICE_NOTE_TITLE_LENGTH];
} ink_voice_note_app_state_t;

typedef struct {
    const ink_voice_note_app_state_t *state;
    const ink_cpfont_t *menu_font;
    const ink_cpfont_t *footer_font;
    const ink_cpfont_t *reader_font;
    char header_meta[24];
} ink_voice_note_app_render_state_t;

const ink_app_descriptor_t *ink_voice_note_app_descriptor(void);
bool ink_voice_note_app_self_test(void);
