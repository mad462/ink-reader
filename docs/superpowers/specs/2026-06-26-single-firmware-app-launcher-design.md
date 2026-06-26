# Single-Firmware App Launcher Design

## Goal

Rework the firmware into a single-firmware App Launcher architecture that boots into a Launcher app, exposes a clean app runtime, and supports switching between a placeholder Reader app and a WiFi Setup app inside one firmware image. The first stage must stop `main/app_main.c` from accumulating page logic, centralize shared services, preserve non-blocking UI and display flow, and reuse the proven WiFi setup behavior from `tests/tilt_grid`.

## Constraints

- ESP-IDF version for this project is `C:\esp\v5.5.4\esp-idf`.
- `IDF_TOOLS_PATH` stays fixed at `C:\Espressif`.
- This stage is single-firmware only. No multi-partition app boot, no OTA app install, no secondary app images.
- Launcher is the default boot app.
- App switching is in-process only. A switch does not reboot the device.
- Display, input, WiFi, fonts, SD card, and NVS are system-owned services. Apps do not initialize or tear them down directly.
- Apps must not talk to the panel driver directly and must not block the main UI loop while waiting on WiFi or long-running work.
- In-app updates should prefer partial refresh. Switching between apps is allowed to force a full refresh.
- Switching apps must flush pending input events and pending display work so stale events do not leak into the new app.
- Stage 1 reader behavior is intentionally minimal: enterable from Launcher and returnable with Back. Existing reader functionality is migrated later.

## Current Structure Assessment

The current main firmware already contains the right low-level execution model for an app runtime:

- `main/app_main.c` owns three tasks:
  - `InputTask` polls hardware buttons and pushes `ink_ui_event_t` into a UI queue.
  - `UiTask` consumes input and display completion events, updates UI state, and submits display work.
  - `EpdTask` consumes the latest display request from `ink_display_mailbox` and performs the panel refresh.
- `components/ink_app_core/ink_display_mailbox.[hc]` already provides a latest-only display work queue with cancellation-aware sequencing.
- `main/ink_app_render.c` already separates display request building from panel execution and has routing for full versus partial refresh.
- `main/ink_app_startup.c` and `main/ink_app_boot.c` already centralize a large part of storage, SD card, font, and reader bootstrap work.

The current application logic is not yet a real runtime:

- `main/ink_app_ui.c` mixes library, reader, and tuning behaviors in one model and one command handler.
- `main/ink_app_priv.h` holds one large `ink_ui_model_t` that combines unrelated page state.
- `main/app_main.c` still knows too much about page-specific behavior through helper calls and command routing.

The WiFi setup prototype under `tests/tilt_grid` has the right functional pieces to reuse:

- `tests/tilt_grid/main/tilt_wifi_setup.[hc]` contains the WiFi setup state machine for list, saved-menu, password, connecting, and result states.
- `tests/tilt_grid/main/app_main.c` already demonstrates:
  - non-blocking WiFi scan/connect/delete worker tasks
  - partial refresh targeting for WiFi list, selection, and keyboard updates
  - queue flushing on modal transitions
  - tilt-driven selection using the MPU path
- `components/ink_net/ink_wifi_manager.[hc]` is already in reusable component form and can stay system-owned.

## Chosen Approach

Use the existing task and mailbox execution model as the system backbone, but replace page-specific UI ownership with a true app runtime layer.

This approach is chosen because it keeps the proven non-blocking display and input flow, avoids a large rewrite of the panel path, and creates clean app boundaries early. It also lets Stage 1 reuse `tests/tilt_grid` WiFi setup logic without forcing the current reader implementation to migrate all at once.

## Runtime Architecture

### System Runtime

Add a system runtime module that becomes the single owner of:

- shared service initialization
- app registration
- active app selection
- app enter and exit transitions
- input queue draining
- display queue draining
- task-to-app event dispatch

The runtime is responsible for high-level flow only:

1. boot shared services
2. register built-in apps
3. enter Launcher
4. feed input events to the active app
5. call active app tick logic on idle periods or timed wakeups
6. ask the active app to produce render work
7. submit render work to the existing display mailbox
8. switch apps when requested

`main/app_main.c` becomes a small composition root that wires together:

- system context
- service initialization
- runtime initialization
- FreeRTOS task creation

It no longer owns library, reader, or WiFi page logic.

### Shared Services

The system exposes a service bundle to apps. Stage 1 service ownership is:

- Display service
  - owns framebuffer allocation
  - owns previous-frame tracking and display mailbox access
  - owns full versus partial refresh submission policy
- Input service
  - owns button polling
  - owns MPU sampling path used by Launcher and WiFi setup
  - normalizes raw hardware events into app-facing events
- Storage service
  - owns NVS init
  - owns SD card mount
  - owns access to app state file paths and fonts
- WiFi service
  - owns `ink_wifi_manager_init()`
  - exposes non-blocking scan/connect/delete helpers to apps
- Font service
  - loads and caches menu, footer, and reader fonts

Apps receive service pointers from the runtime. They do not call panel init, WiFi init, NVS init, or SD mount directly.

### App Interface

Define a uniform app descriptor with this shape:

- `id`
  - stable string identifier such as `launcher`, `reader`, `wifi_setup`
- `name`
  - human-readable label shown by Launcher
- `icon`
  - Stage 1 uses a lightweight icon descriptor suitable for text or bitmap-backed Launcher cards
- `enter`
  - called after the app becomes active
  - may initialize or reset app-local state
  - may request an initial full refresh
- `exit`
  - called before the app is deactivated
  - releases transient app-local state and cancels app-local background intent
- `render`
  - produces an app-owned render description for the current frame
  - does not drive the panel directly
- `input`
  - consumes one normalized app event
  - may mutate app-local state or request an app switch
- `tick`
  - handles time-based behavior such as polling windows, preview timing, or worker-state transitions

The runtime also keeps per-app state storage:

- each app has a private state struct
- the runtime stores one opaque pointer per app
- Stage 1 apps are statically allocated inside the firmware

### App Switch Semantics

App switching is a runtime action, not an app-to-app direct call.

When an app requests a switch:

1. runtime calls current app `exit`
2. runtime drains the UI input queue
3. runtime clears or invalidates pending display mailbox work
4. runtime clears app-level transient display intent
5. runtime sets the new active app
6. runtime calls new app `enter`
7. runtime forces the first frame of the new app to be submitted as a full refresh

This guarantees that button holds, stale partial regions, or old display completions do not cross app boundaries.

### Event Model

Stage 1 runtime events are normalized into a small app-facing set:

- button back
- button confirm
- nav previous
- nav next
- tilt direction previous
- tilt direction next
- display done
- timer tick
- WiFi worker completion

The runtime decides how raw buttons and MPU signals map into these events. Apps only consume the normalized form.

## Stage 1 Apps

### Launcher App

Launcher is the default boot app.

Launcher responsibilities:

- show the installed app list
- highlight one app at a time
- support navigation by left/right buttons and MPU tilt
- enter the selected app on Confirm

Stage 1 Launcher contents:

- `Reader`
- `WiFi Setup`

Stage 1 Launcher rendering can be intentionally simple:

- title at top
- two centered app cards or icon tiles
- selected card with clear visual emphasis
- footer hint row for navigation

Launcher update policy:

- first frame after boot or return from another app uses full refresh
- selection changes may use partial refresh if the dirty region is well bounded
- if partial refresh routing is not yet clean for Launcher cards, a full-window partial fallback is acceptable

### Reader App

Stage 1 Reader app is a placeholder used only to validate runtime switching.

Reader Stage 1 behavior:

- enterable from Launcher
- renders a minimal placeholder page such as `Reader App`, `Phase 1`, `Back to Launcher`
- Back returns to Launcher
- Confirm and nav keys do nothing user-visible in Stage 1

This app does not yet own the current library or XTC reading workflow. That migration is deferred to Stage 2.

### WiFi Setup App

WiFi Setup is a real Stage 1 app and should reuse the proven logic from `tests/tilt_grid`.

Required reused behaviors:

- scan list UI
- saved-network menu
- password keyboard
- connecting state
- result state
- non-blocking WiFi worker flow
- partial refresh targeting for list body, list selection, keyboard selection, and password box

Adaptation rules for integration:

- `tests/tilt_grid` app-global state is split so WiFi setup owns only app-local state
- hardware init is removed from WiFi setup app code and moved to shared services
- worker queues become runtime-owned or system-owned queues with app-scoped payloads
- direct full-screen redraw calls are replaced with runtime render requests
- modal transitions keep the existing behavior of clearing pending display work

WiFi Setup exit behavior:

- abandon transient popup state
- stop treating incoming WiFi worker completions as render triggers if the app is no longer active
- preserve saved credentials because they live in NVS through `ink_wifi_manager`

## Rendering Model

The existing `ink_display_mailbox` stays as the display execution primitive.

Stage 1 rendering flow becomes:

1. active app returns a render description
2. runtime translates it into a display request
3. display request is submitted through `ink_display_mailbox`
4. `EpdTask` renders and refreshes the panel
5. display completion is returned to the runtime and then optionally forwarded to the active app

Stage 1 display policy:

- app switch entry frame: full refresh
- Launcher selection update: partial when bounded, otherwise full-window partial fallback
- WiFi list and keyboard updates: preserve existing local partial strategy from `tests/tilt_grid`
- Reader placeholder updates: full refresh on enter, no routine partial behavior needed yet

Apps do not own the mailbox directly.

## Queue and Worker Policy

There are two queue categories in Stage 1:

- runtime queues
  - UI input queue
  - display mailbox
- app-scoped worker queues
  - WiFi setup worker queue

Switching apps must:

- drain the UI queue
- invalidate pending display work
- discard app-scoped worker completions that arrive after exit

WiFi workers may continue to run briefly after exit, but their completion results must not mutate inactive app UI state. The runtime should gate completion delivery by active app identity.

## File and Module Plan

Stage 1 introduces or reshapes these boundaries:

- `main/app_main.c`
  - reduced to startup composition and task creation
- `main/ink_system_runtime.[hc]`
  - runtime state, app registry, app switching, queue draining, event dispatch
- `main/ink_system_services.[hc]`
  - shared service handles and initialization helpers
- `main/apps/ink_app_iface.h`
  - app descriptor and event types
- `main/apps/ink_launcher_app.[hc]`
  - Launcher app state and behavior
- `main/apps/ink_reader_app.[hc]`
  - Stage 1 placeholder reader
- `main/apps/ink_wifi_setup_app.[hc]`
  - integrated WiFi setup app
- `main/wifi_setup/...`
  - reusable WiFi setup state and drawing helpers extracted from `tests/tilt_grid/main`

Existing modules that remain useful in Stage 1:

- `components/ink_app_core/ink_display_mailbox.[hc]`
- `components/ink_net/ink_wifi_manager.[hc]`
- selected font and storage helpers from `main/ink_app_boot.c`
- selected startup allocation helpers from `main/ink_app_startup.c`
- selected render pipeline pieces from `main/ink_app_render.c`

Existing modules that should stop growing in Stage 1:

- `main/ink_app_ui.c`
- `main/ink_app_priv.h`

Their current mixed library and reader logic should not receive new WiFi or Launcher behavior.

## Migration Plan

### Stage 1

- create the runtime and app interface
- move shared init under system-owned services
- boot into Launcher
- add placeholder Reader app
- integrate WiFi setup as an app using reused `tilt_grid` logic
- keep app switching fully in-process
- preserve existing task and display mailbox model

### Stage 2

- migrate current library and XTC reader logic into a real `reader_app`
- move reader-specific state out of the old monolithic UI model
- keep Back-to-Launcher behavior and app-switch queue flushing

### Stage 3

- consider future partitioning, independent app packaging, or OTA-oriented app delivery only after the in-process runtime is stable

## Testing And Validation

Stage 1 acceptance requires:

- build succeeds under ESP-IDF `v5.5.4`
- flash succeeds to the ESP32-S3 target
- boot reaches Launcher by default
- Launcher can select between Reader and WiFi Setup
- Confirm enters the selected app
- Back from Reader returns to Launcher
- Back from WiFi Setup returns to Launcher from app-level entry states where Back is allowed
- entering WiFi Setup preserves scan, keyboard, and worker behavior
- app switch triggers a full refresh
- stale button or display events do not appear after an app switch
- routine WiFi list and keyboard updates still use partial refresh where already proven

Hardware validation target:

- ESP32-S3
- 16 MB flash
- 8 MB PSRAM
- serial port `COM9`

## Non-Goals For Stage 1

- multiple bootable firmware partitions
- OTA app installation
- dynamically discovered apps from storage
- a complete Reader migration
- background multitasking between apps
- direct app ownership of panel or WiFi initialization
