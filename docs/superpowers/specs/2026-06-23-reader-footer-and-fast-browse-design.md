# Reader Footer And Fast Browse Design

## Goal

Stabilize XTC reader fast-browse so short holds do not overshoot, release lands on the last visibly previewed page, footer text uses space more effectively, and routine logs stay readable.

## Decisions

1. Fast-browse tracks two page numbers:
   - `target_page`: the latest logical browse target.
   - `visible_page`: the last footer preview that actually completed on panel.

2. Releasing the browse key commits to `visible_page` when a preview has landed during the current hold. If no preview landed yet, commit falls back to `target_page`.

3. Footer layout changes to:
   - Left: chapter title.
   - Right: `20% 348/1679`.

4. Fast-browse step policy is made conservative for early hold time:
   - `0~5s -> step=1`
   - `5~10s -> step=5`
   - `10~20s -> step=10`
   - `20s+ -> step=20`

5. Routine logs are reduced to core user-visible flow:
   - keep button, fast browse begin/target/commit, submit, finish, abort/error
   - drop noisy chapter resolve and aggregate stats logs by default

6. Footer partial timing investigation should focus on avoiding unnecessary reset/init work in the fixed footer partial path and measuring against the current `~700ms` baseline.

## Files

- Modify `main/ink_app_fast_browse.c`
- Modify `main/ink_app_priv.h`
- Modify `main/app_main.c`
- Modify `main/ink_app_display_request.c`
- Modify `main/ink_app_render.c`
- Modify `components/ink_app_core/ink_display_mailbox.c`
- Modify `components/ink_book_xtc/ink_reader_session.c`

