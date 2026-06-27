# Reader Long Press Page Turn Design

## Goal

Restore the legacy reader behavior where holding the left or right page-turn key performs continuous real page turns in the current XTC reader page, and releasing the key stops immediately.

## Decisions

1. Reader long press uses real page turns, not footer-only preview.
2. Short press still performs a single normal page turn.
3. Hold-to-repeat starts after a short enter threshold and then emits repeated `NAV_PREVIOUS` or `NAV_NEXT` commands while the key remains down.
4. Repeat speed accelerates modestly with hold duration, but each step still goes through the normal reader page-turn path.
5. Releasing the held direction key stops repetition immediately and does not trigger any final commit jump.
6. Confirm long press keeps its existing white-refresh behavior.
7. The implementation stays inside the existing reader app / UI modules and does not move reader business logic back into `app_main.c`.

## Files

- Modify `main/ink_app_fast_browse.c`
- Modify `main/ink_app_priv.h`
- Modify `main/apps/ink_reader_app.c`
- Modify `main/ink_app_render.c`
