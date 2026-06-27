# Library Tabs And Book Action Design

## Goal

Refit the library home into a three-tab bookshelf experience that matches the current popup/card style and keeps e-ink interaction light:

- default landing tab is `最近`
- tabs are `最近` / `书库` / `收藏`
- `最近` only includes books that have actually been opened for reading
- selecting a book opens a lightweight action popup instead of opening the book immediately
- double-press `Confirm` should remain the fast path into reading:
  - first press opens popup
  - second press activates default `继续阅读` / `开始阅读`

## UX

### Library Page

- Top area contains three tabs:
  - `最近`
  - `书库`
  - `收藏`
- Focus moves across tabs with existing left/right logic when tab focus is active.
- Main content uses compact cards styled consistently with the current reader menu cards.
- Book cards show:
  - optional favorite marker at the start of the title row
  - book title
  - single compact metadata line when available
- Default page entry lands on `最近`.

### Tab Semantics

- `最近`
  - contains only books that have been opened into reading at least once
  - sorted by most recent read time descending
- `书库`
  - contains all books from `/sdcard/books`
- `收藏`
  - contains only books marked favorite
  - removing favorite from this tab should remove the card from the visible list immediately

## Book Action Popup

Opening a book card shows a popup with reduced line count:

- title: full book name, truncated when needed
- progress line: `章节名  页码/总页  百分比`
- primary action:
  - `继续阅读` if the book has saved progress
  - `开始阅读` if first open
- secondary action:
  - `收藏本书` if not favorited
  - `取消收藏` if already favorited

Popup behavior:

- default selected item is always the primary read action
- `Confirm` on primary action opens the book
- `Confirm` on secondary action toggles favorite state in place
- `Back` closes the popup

## State Model

Extend persisted app state with a per-book bookshelf record keyed by book path:

- display title snapshot
- last read page
- last read chapter index/title snapshot
- total pages snapshot
- `has_opened` flag
- `is_favorite` flag
- `last_read_epoch_or_monotonic_order`

Recent ordering uses the saved last-read ordering field and only updates when the user actually enters reading.
Favorite toggles must not affect recent ordering.

## Interaction Model

- Library gains a top-level mode similar to the existing reader menu:
  - tab focus
  - card list focus
  - book action popup focus
- Existing left/right/confirm/back buttons are reused:
  - left/right switch tabs or move list selection depending on focus level
  - confirm enters deeper level / executes selected action
  - back closes popup or returns focus level

## Rendering Strategy

- Reuse current menu/popup visual language from `epd_test_pattern`
- Keep library transitions on the stable full-window stock partial route already adopted for UI screens
- Compact book cards should reuse the same small-font hierarchy as current popup/menu text

## Non-Goals

- no custom cover thumbnails in this phase
- no network-backed metadata
- no fuzzy search
- no separate recent item created by favorite-only action

## Validation

Need self-tests for:

- default library tab is `最近`
- recent list excludes books that are only favorited
- opening a book marks it recent
- favorite toggling updates `收藏` visibility
- popup default action is read/start
- double confirm from card opens popup then enters reading
