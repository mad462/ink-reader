#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INK_APP_EVENT_NONE = 0,
    INK_APP_EVENT_BUTTON_BACK,
    INK_APP_EVENT_BUTTON_CONFIRM,
    INK_APP_EVENT_NAV_PREVIOUS,
    INK_APP_EVENT_NAV_NEXT,
    INK_APP_EVENT_TILT_PREVIOUS,
    INK_APP_EVENT_TILT_NEXT,
    INK_APP_EVENT_DISPLAY_DONE,
    INK_APP_EVENT_TICK,
    INK_APP_EVENT_WIFI_WORK_DONE,
} ink_app_event_kind_t;

typedef struct {
    ink_app_event_kind_t kind;
    uint32_t event_ms;
    int32_t arg0;
    int32_t arg1;
    void *payload;
} ink_app_event_t;

typedef enum {
    INK_APP_RENDER_MODE_NONE = 0,
    INK_APP_RENDER_MODE_LAUNCHER,
    INK_APP_RENDER_MODE_READER_PLACEHOLDER,
    INK_APP_RENDER_MODE_WIFI_SETUP,
} ink_app_render_mode_t;

typedef struct {
    ink_app_render_mode_t mode;
    bool request_full_refresh;
    bool request_partial_refresh;
    int partial_x;
    int partial_y;
    int partial_w;
    int partial_h;
    void *state;
} ink_app_render_model_t;

struct ink_system_services;
struct ink_system_runtime;

typedef struct ink_app_descriptor {
    const char *id;
    const char *name;
    const void *icon;
    void (*enter)(struct ink_system_runtime *runtime, const struct ink_app_descriptor *app);
    void (*exit)(struct ink_system_runtime *runtime, const struct ink_app_descriptor *app);
    bool (*input)(struct ink_system_runtime *runtime, const struct ink_app_descriptor *app, const ink_app_event_t *event);
    bool (*tick)(struct ink_system_runtime *runtime, const struct ink_app_descriptor *app, uint32_t now_ms);
    bool (*render)(struct ink_system_runtime *runtime, const struct ink_app_descriptor *app, ink_app_render_model_t *out_model);
    void *state;
} ink_app_descriptor_t;
