# Mainline Wi-Fi Coordinator Design

## Goal

Before integrating device-side ASR or a future voice-tag app into the main firmware, introduce a single serialized Wi-Fi coordination layer so all modules request Wi-Fi through one owner instead of calling connect, scan, or disconnect paths independently.

This design is meant to reduce three concrete risks already visible from the current direction of the product:

1. multiple modules can race to start or stop Wi-Fi
2. power policy becomes inconsistent when each feature decides its own Wi-Fi lifetime
3. future features such as ASR, time sync, setup, and cloud services become harder to reason about and debug

## Current State

The current mainline already has a useful low-level Wi-Fi wrapper:

- `components/ink_net/ink_wifi_manager.c`
- `components/ink_net/ink_wifi_manager.h`

That layer already owns:

- Wi-Fi init
- STA event handling
- scan
- connect by password
- connect using saved credentials
- connect best known network
- connection status reporting

The current system layer also starts a one-shot background task:

- `main/ink_system_services.c`

That `wifi_auto_connect_task` helps the device connect in the background at boot, but it is not a general-purpose coordination layer. Other modules can still directly call `ink_wifi_manager_*`, which means there is no single arbiter for:

- who currently needs Wi-Fi
- whether Wi-Fi may be turned off
- what request should run next
- how to avoid overlapping scan/connect/reconnect behavior

## Scope

### In scope

- Add a mainline Wi-Fi coordinator task with a request queue
- Make that coordinator the only high-level owner of Wi-Fi session flow
- Keep `ink_wifi_manager` as the low-level Wi-Fi primitive wrapper
- Add a small request model that supports current and near-term use cases
- Add lease-style ownership so one feature can keep Wi-Fi alive while it is still in use
- Centralize idle timeout and disconnect policy
- Define how time sync, Wi-Fi setup, and future ASR should use this layer

### Out of scope

- Voice-tag app UI
- transcript storage
- ASR request implementation inside mainline
- background sync scheduler beyond simple Wi-Fi leasing rules
- multi-interface networking such as AP mode or BLE coexistence strategy

## Options Considered

### Option A: Add a Wi-Fi coordinator task above `ink_wifi_manager`

Modules submit requests to one queue. The coordinator serializes scan/connect/disconnect decisions and owns Wi-Fi lifetime.

Pros:

- clear ownership
- easy to reason about
- maps well to FreeRTOS and the current architecture
- supports power policy cleanly
- good fit for future ASR and periodic sync features

Cons:

- requires some API migration away from direct calls
- introduces a new system service abstraction

### Option B: Keep direct calls, add a mutex and a small state flag set

Pros:

- small code change
- lower short-term migration cost

Cons:

- only prevents simultaneous entry, not policy conflicts
- does not model feature ownership or Wi-Fi lifetime well
- likely to become fragile once more networked features land

### Option C: Keep current structure and handle conflicts feature by feature

Pros:

- fastest short-term path

Cons:

- pushes architectural debt into every future feature
- highest risk of inconsistent power behavior
- harder to test and debug

## Recommendation

Choose Option A.

The right boundary is:

- `ink_wifi_manager`: low-level Wi-Fi primitive layer
- new coordinator: serialized policy and ownership layer
- feature modules: request Wi-Fi work through the coordinator and do not directly manage Wi-Fi lifetime

This keeps the current Wi-Fi code useful while adding the control surface the product now needs.

## Proposed Architecture

### Layer split

#### `ink_wifi_manager`

Keep this layer focused on direct Wi-Fi operations:

- init
- scan
- connect
- disconnect
- read current link status

It should not decide feature priority, queueing, or global idle policy.

#### `ink_wifi_coordinator`

Add a new component-level or mainline service-level module that owns:

- one FreeRTOS task
- one request queue
- one internal state machine
- one active lease table or compact lease set
- one idle timeout policy

Suggested initial location:

- `components/ink_net/ink_wifi_coordinator.c`
- `components/ink_net/ink_wifi_coordinator.h`

That keeps networking concerns together and avoids burying cross-feature policy inside one app.

## Coordinator Responsibilities

The coordinator is responsible for:

1. serializing high-level Wi-Fi requests
2. ensuring only one scan/connect transition runs at a time
3. tracking whether any feature currently holds a Wi-Fi lease
4. deciding when Wi-Fi may stay on, go idle, or disconnect
5. providing synchronous or async completion signals back to callers
6. exposing readable status for diagnostics and UI

The coordinator is not responsible for:

1. performing HTTP requests for apps
2. parsing ASR payloads
3. owning feature retry policies above simple network readiness

## Request Model

The first version should stay small. A useful minimum request set is:

### `ENSURE_CONNECTED`

Guarantee STA mode is initialized and connected to a usable network. If already connected, return quickly.

Used by:

- time sync
- future ASR
- any future cloud-backed feature

### `SCAN`

Run a foreground Wi-Fi scan through the same serialization path.

Used by:

- Wi-Fi setup app

### `CONNECT_SAVED`

Connect to a specific saved credential or best saved credential, depending on request fields.

Used by:

- Wi-Fi setup app
- boot auto-connect path

### `CONNECT_PASSWORD`

Connect to a provided SSID and password, optionally followed by credential save outside or inside the same feature flow.

Used by:

- Wi-Fi setup app

### `RELEASE_LEASE`

Release a previously acquired Wi-Fi use claim.

Used by:

- every feature that explicitly acquired a lease

### `DISCONNECT_IF_IDLE`

Allow explicit reevaluation of Wi-Fi shutdown, but only the coordinator may decide whether the device is truly idle.

Used by:

- internal housekeeping
- optional system power hooks

## Lease Model

The coordinator should treat Wi-Fi use as a lease, not just a boolean lock.

### Why leases matter

Without leases, one module can finish a task and shut Wi-Fi down while another module still depends on it. That is exactly the class of bug we want to prevent before mainline ASR integration.

### First-version lease design

Each request that needs Wi-Fi connectivity may optionally acquire a lease token tied to a small feature identifier, for example:

- `BOOT_AUTO_CONNECT`
- `TIME_SYNC`
- `WIFI_SETUP`
- `VOICE_TAG_ASR`

The coordinator keeps a compact fixed-size lease table. When at least one lease is active:

- Wi-Fi may remain connected
- idle disconnect timer must not fire

When the last lease is released:

- the coordinator starts an idle timer
- if no new request arrives before timeout, Wi-Fi disconnects

### Why fixed-size is enough

The number of real Wi-Fi consumers in this firmware is small and known. A compact fixed array is simpler and safer than a dynamic allocation strategy.

## State Machine

The first version only needs a simple coordinator state machine:

- `OFF`
- `STARTING`
- `DISCONNECTED`
- `CONNECTING`
- `ONLINE`
- `ERROR`

Additional internal flags may track:

- active lease count
- last connected SSID
- last error
- idle deadline
- request in progress

Expected transitions:

1. boot starts in `OFF` or `DISCONNECTED`
2. first connectivity request moves to `STARTING` then `CONNECTING`
3. successful link moves to `ONLINE`
4. last lease released keeps state `ONLINE` until idle timeout expires
5. idle timeout moves to `DISCONNECTED`
6. failures move to `ERROR`, then later requests can retry toward `CONNECTING`

The important design rule is that features react to coordinator results, not to raw `esp_wifi_*` transitions.

## API Direction

The public API should be small and explicit. A likely shape is:

- `ink_wifi_coordinator_init()`
- `ink_wifi_coordinator_start()`
- `ink_wifi_coordinator_request(...)`
- `ink_wifi_coordinator_acquire_lease(...)`
- `ink_wifi_coordinator_release_lease(...)`
- `ink_wifi_coordinator_status(...)`

The exact C signatures can be finalized during implementation planning, but the caller model should support:

1. blocking request with timeout for simple flows
2. status snapshot for UI or diagnostics
3. optional lease-based lifetime control

The important boundary is behavioral, not syntactic:

- features talk to the coordinator
- the coordinator talks to `ink_wifi_manager`

## Integration Targets

### Boot auto-connect

Current `wifi_auto_connect_task` behavior should move behind the coordinator instead of directly calling `ink_wifi_manager_connect_best`.

That means boot auto-connect becomes:

1. coordinator starts
2. system submits a low-priority connect request
3. coordinator owns the actual sequence

### Time sync

Time sync should stop polling raw Wi-Fi state as its main coordination mechanism. Instead it should:

1. request connectivity when it needs to sync
2. perform sync
3. release lease

This keeps time sync power behavior explicit.

### Wi-Fi setup app

The setup app should keep its UI logic but route scan and connect work through the coordinator. This avoids the setup flow colliding with background connect or future ASR operations.

### Future voice-tag ASR

The voice-tag feature should not directly start or stop Wi-Fi. It should:

1. acquire or request an `ENSURE_CONNECTED` lease
2. send ASR request
3. release lease when complete

This is the main reason to do the refactor now rather than after the app lands.

## Power Policy

The first version should implement one simple, predictable policy:

1. Wi-Fi does not stay on just because it was once used
2. active leases keep Wi-Fi available
3. once the last lease is released, start a short idle timeout
4. if no new request arrives during that timeout, disconnect Wi-Fi

Suggested first idle timeout:

- 20 to 60 seconds, implementation decision during planning

This is long enough to avoid needless reconnect churn between adjacent actions, but short enough to remain consistent with a battery-minded product.

The coordinator should be the only place that decides this timeout.

## Error Handling

The coordinator should normalize errors into a small set of outcomes useful to apps:

- `OK`
- `TIMEOUT`
- `NO_CREDENTIAL`
- `CONNECT_FAILED`
- `SCAN_FAILED`
- `BUSY_RETRYABLE`
- `INVALID_STATE`

Apps do not need raw Wi-Fi internals for most decisions. They mainly need to know whether:

1. the network is ready
2. the request failed in a user-visible way
3. a retry makes sense

Detailed raw errors can still be preserved in debug logs and status snapshots.

## Rollout Strategy

This should be implemented in two safe steps.

### Step 1

Introduce the coordinator and move only:

- boot auto-connect
- time sync

This validates the service boundary with low-risk consumers first.

### Step 2

Move interactive feature flows:

- Wi-Fi setup app scan/connect
- future voice-tag ASR

This avoids changing all Wi-Fi consumers at once.

## Testing Strategy

Minimum coverage for this refactor should include:

1. coordinator self-test for state transitions and lease accounting
2. request ordering test with overlapping callers
3. idle timeout behavior after final lease release
4. repeated `ENSURE_CONNECTED` requests while already online
5. scan request while another feature holds connectivity
6. boot auto-connect still succeeds through the coordinator
7. time sync can request, use, and release connectivity cleanly

Manual validation should include:

1. cold boot with saved Wi-Fi
2. Wi-Fi setup scan/connect flow
3. one-hour time sync still behaves correctly
4. future ASR integration smoke test after this layer lands

## Risks And Tradeoffs

### Migration risk

For a short period, old direct-call paths and new coordinator paths may coexist. During rollout, that must be controlled carefully. The end state should be that feature code no longer directly calls `ink_wifi_manager_*` for high-level behavior.

### Slightly more moving parts

The coordinator adds a task and queue, but this is deliberate complexity that replaces future uncontrolled complexity across many modules.

### Boot behavior may change subtly

Because auto-connect now becomes a queued policy action, timing may shift slightly. That is acceptable as long as user-visible startup remains stable.

## Acceptance Criteria

This design is successful when:

1. mainline has one clear owner for Wi-Fi policy and lifetime
2. feature modules no longer make independent high-level Wi-Fi decisions
3. time sync, setup, and future ASR can share Wi-Fi without conflicting shutdown behavior
4. idle disconnect behavior is centralized and predictable
5. this layer can be integrated without coupling it to voice-tag app logic
