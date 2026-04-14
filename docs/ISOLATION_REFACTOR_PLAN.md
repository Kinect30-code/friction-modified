# Isolation Refactor Plan

## Goal

Keep the project close to upstream Friction behavior, and move custom features into isolated extension-style layers so they can be removed, ported, or upstreamed with low risk.

## Key Findings

### 1. Project version mismatch is real and local

The modified build currently writes EV project format version `35` because [evformat.h](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/core/ReadWrite/evformat.h) defines:

- `grid = 34`
- `timelineColor = 35`
- `version = nextVersion - 1`

That is why older Friction builds that only support version `34` refuse to open the project.

This is not caused by ORA/WEBM/puppet directly. It is caused by the core EV serializer compatibility level.

### 2. Some custom work is already naturally isolated

These features already sit near existing extension points instead of deep core math:

- ORA import: [eimporters.cpp](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/app/eimporters.cpp)
- Import registration layer: [importhandler.h](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/core/importhandler.h)
- WEBM alpha edge fix: [videoframeloader.cpp](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/core/FileCacheHandlers/videoframeloader.cpp)
- Puppet system: [puppeteffect.h](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/core/RasterEffects/puppeteffect.h)
- Parenting via official effect path: `ParentEffect`

These are good candidates for “plugin-like” modules.

### 3. The dangerous coupling is mostly in interaction glue

The high-risk area is not the effect classes themselves. It is the UI and canvas interaction glue that mixes feature routing, AE-like behavior, selection sync, and object creation.

Main hotspots:

- [canvasmouseinteractions.cpp](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/core/canvasmouseinteractions.cpp)
- [canvashandlesmartpath.cpp](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/core/canvashandlesmartpath.cpp)
- timeline shortcut / expansion handlers
- a few added helpers in core box/effect headers

This is where logic feels “乱” even when the underlying feature is valid.

## Core Rules For Refactor

### Do not touch unless absolutely necessary

These should be treated as protected core:

- transform math
- serialization core for existing upstream fields
- parent-child matrix logic unless using official existing effect path
- base timeline selection model
- box geometry primitives

### Prefer existing Friction mechanisms first

Before adding any behavior:

1. Check whether Friction already has an effect/property/import path for it.
2. If yes, adapt UI routing to that path.
3. Only add new core data structures if there is no existing host.

## Isolation Targets

### A. ORA module

Current state:
- Mostly concentrated in [eimporters.cpp](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/app/eimporters.cpp)

Target shape:
- `src/modules/ora/`
- one importer entry
- one metadata helper
- one scene/folder assembly helper

Keep core dependency one-way:
- module depends on `ImportHandler`, `Canvas`, `Document`
- core must not know ORA-specific scene policies

### B. WEBM support module

Current state:
- import support is mostly generic already
- custom behavior is a frame conversion fix in [videoframeloader.cpp](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/core/FileCacheHandlers/videoframeloader.cpp)

Target shape:
- keep decoding in core cache path
- move WebM-specific alpha normalization into a named helper, for example `videoalphapolicy.*`
- make the loader call a policy/helper instead of embedding format-specific logic inline

This is “module-like” even if it stays compiled in.

### C. Puppet module

Current state:
- effect itself is relatively self-contained in [puppeteffect.cpp](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/core/RasterEffects/puppeteffect.cpp)
- creation logic is routed from canvas interaction code

Target shape:
- `src/modules/puppet/`
- puppet tool routing
- pin creation helpers
- mesh preview helpers
- effect registration hook

Core keeps only:
- the effect host interface
- raster effect invocation path

### D. AE mask workflow module

Current state:
- most logic is concentrated in canvas interaction files, not in a formal module
- storage conventions like `Masks` and `__AE_LAYER_MASKS__` are spread across interaction code

Target shape:
- `src/modules/ae_masks/`
- mask target resolver
- mask creation policy
- mask storage naming policy
- selection sync helpers
- conversion helpers for shape-to-path and mask-path focus

This is the biggest cleanup target.

## Recommended Migration Order

### Phase 1. Freeze compatibility

1. Stop adding new serialized core fields.
2. Decide whether to temporarily keep EV save version at `34` for compatibility.
3. If version `35` only represents local UI/state additions, downgrade or gate those fields.

### Phase 2. Extract interaction glue

Move feature-specific helper code out of:
- [canvasmouseinteractions.cpp](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/core/canvasmouseinteractions.cpp)
- [canvashandlesmartpath.cpp](/media/kinect/Project/friction-1.0.0-需要重构的版本/src/core/canvashandlesmartpath.cpp)

Into module-style files with narrow APIs.

### Phase 3. Register modules through existing extension points

- importers through `ImportHandler`
- effects through existing effect collections/menus
- tool behavior through a thin tool dispatcher layer

### Phase 4. Restore upstream parity where possible

For each custom feature:
- compare against upstream Friction capability
- remove duplicate custom implementations where upstream already has a host mechanism

## Immediate Safe Next Steps

1. Audit why EV version `35` is needed in this fork.
2. Make a compatibility decision for saving `.ev` files.
3. Extract AE mask helper logic out of canvas interaction files first.
4. Then extract ORA importer helpers.
5. Then extract puppet tool routing.

## What This Means Practically

Good news:
- the project is messy, but not hopeless
- several of your expensive custom features are already close to effect/import/tool boundaries
- we do not need to rewrite the whole app to save them

Bad news:
- the interaction layer is currently the main source of instability
- if we keep adding features there, regressions will continue

## Suggested Working Rule From Now On

Any new feature must answer these three questions before code is written:

1. What existing Friction host mechanism will own it
2. What file will be the single feature entry point
3. Can we delete the whole feature by removing one module folder and one registration call
