#include "apps/ink_photo_album_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apps/ink_launcher_app.h"
#include "ink_photo_bmp_parser.h"
#include "ink_photo_catalog.h"
#include "ink_system_runtime.h"
#include "ink_system_services.h"
#include "epd_gdey0426t82.h"

static ink_photo_album_app_state_t s_photo_album_state;
static ink_photo_album_render_state_t s_photo_album_render_state;

static void photo_album_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static void photo_album_exit(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app);
static bool photo_album_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event);
static bool photo_album_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model);
static bool request_switch_to_launcher(ink_system_runtime_t *runtime);
static void album_set_status(ink_photo_album_app_state_t *state, const char *text);
static void album_clear_partial_refresh(ink_photo_album_app_state_t *state);
static void album_set_partial_refresh(
    ink_photo_album_app_state_t *state,
    int x,
    int y,
    int w,
    int h);
static bool album_sync_catalog(
    ink_photo_album_app_state_t *state,
    const ink_system_services_t *services);
static bool album_load_current_image(ink_photo_album_app_state_t *state);
static bool album_ensure_planes(ink_photo_album_app_state_t *state);
static bool photo_album_default_preview_self_test(void);
static bool photo_album_nav_self_test(void);
static bool photo_album_confirm_switch_self_test(void);
static bool photo_album_back_self_test(void);
static bool photo_album_reenter_preserves_index_self_test(void);
static bool photo_album_list_confirm_requests_full_refresh_self_test(void);

static const ink_app_descriptor_t kPhotoAlbumApp = {
    .id = "photo_album",
    .name = "Photo Album",
    .enter = photo_album_enter,
    .exit = photo_album_exit,
    .input = photo_album_input,
    .render = photo_album_render,
    .state = &s_photo_album_state,
};

const ink_app_descriptor_t *ink_photo_album_app_descriptor(void)
{
    return &kPhotoAlbumApp;
}

static bool request_switch_to_launcher(ink_system_runtime_t *runtime)
{
    const ink_app_descriptor_t *launcher = ink_system_runtime_find_app_by_id(runtime, "launcher");

    return launcher != NULL
        && ink_system_runtime_request_switch(runtime, launcher);
}

static void album_set_status(ink_photo_album_app_state_t *state, const char *text)
{
    if (state == NULL) {
        return;
    }

    snprintf(state->status_text, sizeof(state->status_text), "%s", text != NULL ? text : "");
}

static void album_clear_partial_refresh(ink_photo_album_app_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->partial_refresh_pending = false;
    state->partial_x = 0;
    state->partial_y = 0;
    state->partial_w = 0;
    state->partial_h = 0;
}

static void album_set_partial_refresh(
    ink_photo_album_app_state_t *state,
    int x,
    int y,
    int w,
    int h)
{
    if (state == NULL) {
        return;
    }

    state->partial_refresh_pending = true;
    state->partial_x = x;
    state->partial_y = y;
    state->partial_w = w;
    state->partial_h = h;
}

static bool album_ensure_planes(ink_photo_album_app_state_t *state)
{
    if (state == NULL) {
        return false;
    }
    if (state->lsb_plane != NULL && state->msb_plane != NULL) {
        return true;
    }

    state->plane_size = EPD_GDEY0426T82_GRAY_PLANE_SIZE;
    state->lsb_plane = (uint8_t *)malloc(state->plane_size);
    state->msb_plane = (uint8_t *)malloc(state->plane_size);
    if (state->lsb_plane == NULL || state->msb_plane == NULL) {
        free(state->lsb_plane);
        free(state->msb_plane);
        state->lsb_plane = NULL;
        state->msb_plane = NULL;
        state->plane_size = 0U;
        album_set_status(state, "图片缓冲不足");
        return false;
    }

    memset(state->lsb_plane, 0x00, state->plane_size);
    memset(state->msb_plane, 0x00, state->plane_size);
    return true;
}

static bool album_sync_catalog(
    ink_photo_album_app_state_t *state,
    const ink_system_services_t *services)
{
    const ink_photo_catalog_t *catalog = NULL;

    if (state == NULL || services == NULL) {
        return false;
    }

    catalog = &services->photo_catalog;
    state->catalog_ready = catalog->directory_ready;
    state->tf_unavailable = !services->tf_ready || !catalog->directory_ready;
    state->total_count = catalog->count;
    if (state->tf_unavailable) {
        state->image_loaded = false;
        state->load_failed = false;
        album_set_status(state, "TF 不可用");
        return false;
    }
    if (state->total_count == 0U) {
        state->image_loaded = false;
        state->load_failed = false;
        album_set_status(state, "photos 目录为空");
        return true;
    }

    if (state->current_index >= state->total_count) {
        state->current_index = state->total_count - 1U;
    }
    if (state->list_selected_index >= state->total_count) {
        state->list_selected_index = state->current_index;
    }
    return true;
}

static bool album_load_current_image(ink_photo_album_app_state_t *state)
{
    char error_text[64];

    if (state == NULL) {
        return false;
    }
    if (state->total_count == 0U) {
        state->image_loaded = false;
        state->load_failed = false;
        album_set_status(state, "photos 目录为空");
        return true;
    }
    if (!album_ensure_planes(state)) {
        state->image_loaded = false;
        state->load_failed = true;
        return false;
    }
    if (!ink_photo_parse_4gray_bmp_to_planes(
            state->current_path,
            state->lsb_plane,
            state->plane_size,
            state->msb_plane,
            state->plane_size,
            error_text,
            sizeof(error_text))) {
        state->image_loaded = false;
        state->load_failed = true;
        album_set_status(
            state,
            error_text[0] != '\0' ? error_text : "图片读取失败");
        return false;
    }

    state->image_loaded = true;
    state->load_failed = false;
    album_set_status(state, state->current_name);
    return true;
}

static void photo_album_enter(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_photo_album_app_state_t *state = NULL;
    const ink_system_services_t *services = NULL;
    const ink_photo_catalog_t *catalog = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL) {
        return;
    }

    state = (ink_photo_album_app_state_t *)app->state;
    services = runtime->services;
    if (!state->initialized) {
        memset(state, 0, sizeof(*state));
        state->initialized = true;
    }

    state->view_mode = INK_PHOTO_ALBUM_VIEW_PREVIEW;
    album_clear_partial_refresh(state);
    s_photo_album_render_state.state = state;
    s_photo_album_render_state.catalog = services != NULL ? &services->photo_catalog : NULL;
    s_photo_album_render_state.menu_font = services != NULL ? &services->menu_font : NULL;
    s_photo_album_render_state.footer_font = services != NULL ? &services->footer_font : NULL;

    if (!album_sync_catalog(state, services)) {
        runtime->force_full_refresh_on_next_render = true;
        return;
    }

    if (services == NULL) {
        album_set_status(state, "TF 不可用");
        runtime->force_full_refresh_on_next_render = true;
        return;
    }
    catalog = &services->photo_catalog;
    if (state->total_count == 0U) {
        runtime->force_full_refresh_on_next_render = true;
        return;
    }

    if (state->current_index >= catalog->count) {
        state->current_index = catalog->count - 1U;
    }
    state->list_selected_index = state->current_index;
    (void)ink_photo_catalog_copy_name(
        catalog,
        state->current_index,
        state->current_name,
        sizeof(state->current_name));
    (void)ink_photo_catalog_copy_path(
        catalog,
        state->current_index,
        state->current_path,
        sizeof(state->current_path));
    (void)album_load_current_image(state);
    runtime->force_full_refresh_on_next_render = true;
}

static void photo_album_exit(ink_system_runtime_t *runtime, const ink_app_descriptor_t *app)
{
    ink_photo_album_app_state_t *state = NULL;

    (void)runtime;
    if (app == NULL || app->state == NULL) {
        return;
    }

    state = (ink_photo_album_app_state_t *)app->state;
    album_clear_partial_refresh(state);
}

static bool photo_album_input(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    const ink_app_event_t *event)
{
    ink_photo_album_app_state_t *state = NULL;
    const ink_photo_catalog_t *catalog = NULL;
    size_t next_index = 0U;

    if (runtime == NULL || app == NULL || app->state == NULL || event == NULL) {
        return false;
    }

    state = (ink_photo_album_app_state_t *)app->state;
    catalog = runtime->services != NULL ? &runtime->services->photo_catalog : NULL;

    if (event->kind == INK_APP_EVENT_BUTTON_BACK) {
        return request_switch_to_launcher(runtime);
    }
    if (state->total_count == 0U) {
        if (event->kind == INK_APP_EVENT_BUTTON_CONFIRM) {
            state->view_mode = state->view_mode == INK_PHOTO_ALBUM_VIEW_PREVIEW
                ? INK_PHOTO_ALBUM_VIEW_LIST
                : INK_PHOTO_ALBUM_VIEW_PREVIEW;
            return true;
        }
        return false;
    }

    switch (state->view_mode) {
        case INK_PHOTO_ALBUM_VIEW_PREVIEW:
            switch (event->kind) {
                case INK_APP_EVENT_NAV_PREVIOUS:
                case INK_APP_EVENT_TILT_PREVIOUS:
                    next_index = (state->current_index + state->total_count - 1U) % state->total_count;
                    state->current_index = next_index;
                    state->list_selected_index = next_index;
                    (void)ink_photo_catalog_copy_name(catalog, next_index, state->current_name, sizeof(state->current_name));
                    (void)ink_photo_catalog_copy_path(catalog, next_index, state->current_path, sizeof(state->current_path));
                    (void)album_load_current_image(state);
                    return true;
                case INK_APP_EVENT_NAV_NEXT:
                case INK_APP_EVENT_TILT_NEXT:
                    next_index = (state->current_index + 1U) % state->total_count;
                    state->current_index = next_index;
                    state->list_selected_index = next_index;
                    (void)ink_photo_catalog_copy_name(catalog, next_index, state->current_name, sizeof(state->current_name));
                    (void)ink_photo_catalog_copy_path(catalog, next_index, state->current_path, sizeof(state->current_path));
                    (void)album_load_current_image(state);
                    return true;
                case INK_APP_EVENT_BUTTON_CONFIRM:
                    state->view_mode = INK_PHOTO_ALBUM_VIEW_LIST;
                    state->list_selected_index = state->current_index;
                    album_clear_partial_refresh(state);
                    runtime->force_full_refresh_on_next_render = true;
                    return true;
                default:
                    return false;
            }
        case INK_PHOTO_ALBUM_VIEW_LIST:
            switch (event->kind) {
                case INK_APP_EVENT_NAV_PREVIOUS:
                case INK_APP_EVENT_TILT_PREVIOUS:
                    state->list_selected_index =
                        (state->list_selected_index + state->total_count - 1U) % state->total_count;
                    album_set_partial_refresh(state, 0, 88, EPD_GDEY0426T82_WIDTH, 690);
                    return true;
                case INK_APP_EVENT_NAV_NEXT:
                case INK_APP_EVENT_TILT_NEXT:
                    state->list_selected_index = (state->list_selected_index + 1U) % state->total_count;
                    album_set_partial_refresh(state, 0, 88, EPD_GDEY0426T82_WIDTH, 690);
                    return true;
                case INK_APP_EVENT_BUTTON_CONFIRM:
                    state->current_index = state->list_selected_index;
                    (void)ink_photo_catalog_copy_name(
                        catalog,
                        state->current_index,
                        state->current_name,
                        sizeof(state->current_name));
                    (void)ink_photo_catalog_copy_path(
                        catalog,
                        state->current_index,
                        state->current_path,
                        sizeof(state->current_path));
                    (void)album_load_current_image(state);
                    state->view_mode = INK_PHOTO_ALBUM_VIEW_PREVIEW;
                    album_clear_partial_refresh(state);
                    runtime->force_full_refresh_on_next_render = true;
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

static bool photo_album_render(
    ink_system_runtime_t *runtime,
    const ink_app_descriptor_t *app,
    ink_app_render_model_t *out_model)
{
    ink_photo_album_app_state_t *state = NULL;

    if (runtime == NULL || app == NULL || app->state == NULL || out_model == NULL) {
        return false;
    }

    state = (ink_photo_album_app_state_t *)app->state;
    memset(out_model, 0, sizeof(*out_model));
    out_model->mode = INK_APP_RENDER_MODE_PHOTO_ALBUM;
    out_model->request_full_refresh = runtime->force_full_refresh_on_next_render;
    out_model->request_partial_refresh = !out_model->request_full_refresh
        && state->partial_refresh_pending;
    out_model->partial_x = state->partial_x;
    out_model->partial_y = state->partial_y;
    out_model->partial_w = state->partial_w;
    out_model->partial_h = state->partial_h;
    out_model->state = &s_photo_album_render_state;
    runtime->force_full_refresh_on_next_render = false;
    album_clear_partial_refresh(state);
    return true;
}

static bool photo_album_default_preview_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_photo_album_app_state_t *state = &s_photo_album_state;
    const ink_app_descriptor_t *app = ink_photo_album_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    runtime.services = &(ink_system_services_t){0};
    if (!ink_system_runtime_register_app(&runtime, app)
        || !ink_system_runtime_set_active_app(&runtime, app)) {
        return false;
    }

    return state->view_mode == INK_PHOTO_ALBUM_VIEW_PREVIEW;
}

static bool photo_album_nav_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_system_services_t services;
    ink_photo_catalog_t *catalog = &services.photo_catalog;
    ink_photo_album_app_state_t *state = &s_photo_album_state;
    const ink_app_descriptor_t *app = ink_photo_album_app_descriptor();
    ink_app_event_t event = {.kind = INK_APP_EVENT_NAV_NEXT};

    memset(&services, 0, sizeof(services));
    ink_photo_catalog_init(catalog);
    catalog->directory_ready = true;
    catalog->count = 3U;
    snprintf(catalog->entries[0].name, sizeof(catalog->entries[0].name), "%s", "A.bmp");
    snprintf(catalog->entries[1].name, sizeof(catalog->entries[1].name), "%s", "B.bmp");
    snprintf(catalog->entries[2].name, sizeof(catalog->entries[2].name), "%s", "C.bmp");
    snprintf(catalog->entries[0].path, sizeof(catalog->entries[0].path), "%s", "/sdcard/photos/A.bmp");
    snprintf(catalog->entries[1].path, sizeof(catalog->entries[1].path), "%s", "/sdcard/photos/B.bmp");
    snprintf(catalog->entries[2].path, sizeof(catalog->entries[2].path), "%s", "/sdcard/photos/C.bmp");
    services.tf_ready = true;
    memset(state, 0, sizeof(*state));
    state->initialized = true;
    state->total_count = 3U;
    ink_system_runtime_init(&runtime);
    runtime.services = &services;

    if (!photo_album_input(&runtime, app, &event)
        || state->current_index != 1U) {
        return false;
    }

    event.kind = INK_APP_EVENT_NAV_PREVIOUS;
    return photo_album_input(&runtime, app, &event)
        && state->current_index == 0U;
}

static bool photo_album_confirm_switch_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_photo_album_app_state_t *state = &s_photo_album_state;
    const ink_app_descriptor_t *app = ink_photo_album_app_descriptor();
    ink_app_event_t event = {.kind = INK_APP_EVENT_BUTTON_CONFIRM};

    memset(state, 0, sizeof(*state));
    state->initialized = true;
    state->total_count = 2U;
    state->view_mode = INK_PHOTO_ALBUM_VIEW_PREVIEW;
    ink_system_runtime_init(&runtime);

    if (!photo_album_input(&runtime, app, &event)
        || state->view_mode != INK_PHOTO_ALBUM_VIEW_LIST) {
        return false;
    }

    return true;
}

static bool photo_album_back_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_photo_album_app_state_t *state = &s_photo_album_state;
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *album = ink_photo_album_app_descriptor();
    ink_app_event_t event = {.kind = INK_APP_EVENT_BUTTON_BACK};

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, album)) {
        return false;
    }

    state->view_mode = INK_PHOTO_ALBUM_VIEW_PREVIEW;
    if (!photo_album_input(&runtime, album, &event)
        || runtime.pending_app != launcher
        || !runtime.force_full_refresh_on_next_render) {
        return false;
    }

    runtime.pending_app = NULL;
    runtime.force_full_refresh_on_next_render = false;
    state->view_mode = INK_PHOTO_ALBUM_VIEW_LIST;
    return photo_album_input(&runtime, album, &event)
        && runtime.pending_app == launcher
        && runtime.force_full_refresh_on_next_render;
}

static bool photo_album_reenter_preserves_index_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_photo_album_app_state_t *state = &s_photo_album_state;
    const ink_app_descriptor_t *launcher = ink_launcher_app_descriptor();
    const ink_app_descriptor_t *album = ink_photo_album_app_descriptor();

    memset(state, 0, sizeof(*state));
    ink_system_runtime_init(&runtime);
    if (!ink_system_runtime_register_app(&runtime, launcher)
        || !ink_system_runtime_register_app(&runtime, album)
        || !ink_system_runtime_set_active_app(&runtime, album)) {
        return false;
    }

    state->current_index = 2U;
    if (!ink_system_runtime_switch_now(&runtime, launcher)
        || !ink_system_runtime_switch_now(&runtime, album)) {
        return false;
    }

    return state->current_index == 2U;
}

static bool photo_album_list_confirm_requests_full_refresh_self_test(void)
{
    ink_system_runtime_t runtime;
    ink_system_services_t services;
    ink_photo_catalog_t *catalog = &services.photo_catalog;
    ink_photo_album_app_state_t *state = &s_photo_album_state;
    const ink_app_descriptor_t *app = ink_photo_album_app_descriptor();
    ink_app_event_t event = {.kind = INK_APP_EVENT_BUTTON_CONFIRM};

    memset(&services, 0, sizeof(services));
    ink_photo_catalog_init(catalog);
    catalog->directory_ready = true;
    catalog->count = 1U;
    snprintf(catalog->entries[0].name, sizeof(catalog->entries[0].name), "%s", "A.bmp");
    snprintf(catalog->entries[0].path, sizeof(catalog->entries[0].path), "%s", "/sdcard/photos/A.bmp");
    services.tf_ready = true;

    memset(state, 0, sizeof(*state));
    state->initialized = true;
    state->view_mode = INK_PHOTO_ALBUM_VIEW_LIST;
    state->total_count = 1U;
    ink_system_runtime_init(&runtime);
    runtime.services = &services;
    runtime.force_full_refresh_on_next_render = false;

    if (!photo_album_input(&runtime, app, &event)) {
        return false;
    }

    return state->view_mode == INK_PHOTO_ALBUM_VIEW_PREVIEW
        && runtime.force_full_refresh_on_next_render;
}

bool ink_photo_album_app_self_test(void)
{
    return photo_album_default_preview_self_test()
        && photo_album_nav_self_test()
        && photo_album_confirm_switch_self_test()
        && photo_album_back_self_test()
        && photo_album_reenter_preserves_index_self_test()
        && photo_album_list_confirm_requests_full_refresh_self_test();
}
