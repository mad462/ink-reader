# EPUB Multi Preview Design

Date: 2026-06-21
Project: ink-reader
Scope: `tools/epub-to-xtc-converter` multi-page preview and clearer chapter page-order validation

## Problem

Two usability issues remain in the converter:

1. Chapter save failures currently report only a generic descending-page error, which makes long TOCs hard to fix.
2. The preview area only shows one page at a time, making chapter alignment work slower than necessary.

## Decision

Add a toggleable `10页预览` mode inside the existing preview area and improve chapter validation messages so they surface the exact conflicting page values.

## UX

- Add a `10页预览` button in the preview header.
- When enabled, the preview area renders up to 10 continuous pages starting from the current page.
- Each page card shows the rendered page plus a page-number caption underneath.
- Clicking a page card updates `currentPage`, refreshes highlight/progress/chapter state, and stays in 10-page mode.
- The existing single-page preview remains available and is the default mode.
- Descending chapter-page validation keeps blocking save, but the message now names the previous and current page values that conflict.

