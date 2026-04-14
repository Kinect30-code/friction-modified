# Friction First-Party Audit

This document exists to make debugging less guess-based.

The project currently mixes complex UI state, shortcut routing, and timeline visibility logic across several layers. That makes small regressions feel random even when the internal state actually changed correctly.

## Recommended Workflow

Generate compile commands once:

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Run focused first-party analysis:

```bash
tools/run_cppcheck_focus.sh gui-timeline
tools/run_cppcheck_focus.sh gui
tools/run_cppcheck_focus.sh firstparty
```

The script intentionally:

- uses `build/compile_commands.json`
- ignores third-party noise through a suppression list
- filters final output to `src/app`, `src/core`, and `src/ui`
- keeps a raw report and a filtered report under `/tmp`

## Latest Run

On 2026-03-29, `tools/run_cppcheck_focus.sh gui-timeline` completed successfully and produced:

- filtered report: `/tmp/friction_cppcheck_gui-timeline.txt`
- filtered issue count: `0`

That does not mean the timeline code is clean. It means the current shortcut/toggle bugs are more likely caused by event routing, stale widget state, or reveal-state coupling than by the kind of defects `cppcheck` catches well.

## Why This Is Needed

Raw `cppcheck --project=build/compile_commands.json` output is dominated by:

- Skia headers pulled in through include chains
- Qt macro parsing noise such as `Q_UNUSED`
- system-header portability warnings that are not useful for current debugging

Without filtering, the report is large but low-signal.

## Current Architecture Risks

### 1. Shortcut routing is spread across too many layers

Relevant files:

- `src/app/GUI/mainwindow.cpp`
- `src/app/GUI/timelinedockwidget.cpp`
- `src/app/GUI/timelinewidget.cpp`
- `src/app/GUI/BoxesList/boxscroller.cpp`
- `src/app/GUI/keysview.cpp`

Current routing is effectively stacked like this:

1. `MainWindow::eventFilter()`
2. `TimelineDockWidget::processKeyPress()`
3. `TimelineWidget::eventFilter()`
4. `TimelineWidget` local `QShortcut` objects
5. downstream widget handlers such as `KeysView` and `BoxScrollWidget`

This layering explains why shortcut debugging has felt unstable: a key can be accepted by one layer, visually contradicted by another layer, or handled differently depending on focus and cursor position.

### 2. Visibility state and visible UI are not the same thing

Relevant files:

- `src/app/GUI/BoxesList/boxscrollwidget.cpp`
- `src/app/GUI/BoxesList/boxsinglewidget.cpp`
- `src/core/swt_abstraction.cpp`

`U` toggle behavior already proved that the underlying transform section state can flip correctly. The remaining failures are likely in the presentation layer:

- collapsed-summary widgets still showing stale state
- child abstraction visibility not matching parent row visibility
- reveal presets and manual expansion both mutating the same state graph

This is a classic “model changed, widget tree did not fully refresh” problem.

### 3. Easing UI depends on expression presets

Relevant files:

- `src/app/GUI/keysview.cpp`
- `src/core/Expressions/expressionpresets.cpp`

Quick easing is not a pure timeline feature. It depends on expression presets being present and enabled. That coupling is easy to miss during debugging and makes a timeline action fail for reasons that live in settings state.

### 4. AE-style reveal logic is stateful and global

Relevant files:

- `src/app/GUI/BoxesList/boxscrollwidget.cpp`

`applyAeRevealPreset()`, `dismissAeRevealPreset()`, `restoreAeRevealState()`, and `reapplyAeRevealPreset()` cache visibility/expansion state and then overwrite normal display rules. This is useful for UX, but it means:

- selection changes can rewrite panel state globally
- unrelated UI refreshes can resurrect stale expansion state
- debug sessions become hard because the timeline is not driven by a single source of truth

## Current Verified Runtime Facts

These were already confirmed by runtime logs:

- `TimelineDockWidget::currentTimelineWidget()` resolves the expected timeline widget.
- pressing `U` reaches the timeline path
- `BoxScrollWidget::toggleSelectedTransformVisibility()` flips transform abstraction visibility from `false -> true -> false`

That means the remaining `U` issue is not “shortcut did not fire”. It is a display consistency problem somewhere after the state toggle.

## What To Check Next

### Highest value cleanup

- reduce shortcut ownership to one main entry path for timeline-only actions
- remove duplicate handling between `MainWindow`, `TimelineDockWidget`, `TimelineWidget::eventFilter()`, and local `QShortcut`s
- separate “temporary reveal for AE shortcut” state from persistent row expansion state
- make one explicit refresh path after toggling property visibility

### Good candidate functions for review

- `MainWindow::eventFilter()`
- `TimelineDockWidget::processKeyPress()`
- `TimelineWidget::handleAeShortcutEvent()`
- `TimelineWidget::eventFilter()`
- `BoxScrollWidget::toggleSelectedTransformVisibility()`
- `BoxScrollWidget::applyAeRevealPreset()`
- `BoxScrollWidget::restoreAeRevealState()`
- `KeysView::applyQuickEasingPreset()`

## Notes For Future Bug Hunts

- If a key “does nothing”, verify whether the action failed, or whether the state changed but the UI did not repaint.
- If a timeline easing action says a preset is unavailable, inspect expression preset enablement before changing timeline code.
- If a filtered cppcheck report is empty, that does not mean the module is healthy. It usually means the current problem is behavioral or architectural rather than a classic static-analysis defect.
