# Isolation Code Map

## Goal

This map documents the current "pseudo-plugin" isolation work so future changes can stay out of core code unless absolutely necessary.

The rule from now on is:

1. Prefer an existing Friction host mechanism.
2. Put custom behavior behind one module entry point.
3. Keep core files as thin dispatchers instead of feature owners.

## Current Module Boundaries

### 1. AE Mask Module

Module folder:

- [src/modules/ae_masks](/media/kinect/Project/friction-1.0.1/src/modules/ae_masks)

Public entry points:

- [aemaskmodule.h](/media/kinect/Project/friction-1.0.1/src/modules/ae_masks/aemaskmodule.h)

What this module owns:

- deciding whether the current selection should draw a mask
- generating unique mask names
- attaching `LayerMaskEffect` to the target layer
- finalizing editable mask-path state
- syncing selected layer and visible mask-path properties

Main implementation:

- [aemaskmodule.cpp](/media/kinect/Project/friction-1.0.1/src/modules/ae_masks/aemaskmodule.cpp)

Thin host glue still in core:

- [canvasmouseinteractions.cpp](/media/kinect/Project/friction-1.0.1/src/core/canvasmouseinteractions.cpp)
- [canvashandlesmartpath.cpp](/media/kinect/Project/friction-1.0.1/src/core/canvashandlesmartpath.cpp)

What the glue does now:

- tool routing
- selection handoff
- creating the initial path object from a canvas gesture
- calling `AeMaskModule::*` at the right moment

What can still be extracted later:

- AE shape-layer routing helpers currently still local to `canvasmouseinteractions.cpp`
- mask-path canvas setup duplicated around smart-path creation

### 2. ORA Import Module

Module folder:

- [src/modules/ora](/media/kinect/Project/friction-1.0.1/src/modules/ora)

Public entry points:

- [oramodule.h](/media/kinect/Project/friction-1.0.1/src/modules/ora/oramodule.h)

Main implementation:

- [oramodule.cpp](/media/kinect/Project/friction-1.0.1/src/modules/ora/oramodule.cpp)

What this module owns:

- ORA archive extraction
- cache directory naming
- import metadata persistence
- hot reload watching
- XML stack parsing
- converting nested ORA stacks into Friction scenes/precomps

Thin host glue:

- [eimporters.cpp](/media/kinect/Project/friction-1.0.1/src/app/eimporters.cpp)

What the glue does now:

- keeps importer registration in the app layer
- forwards ORA imports to `OraModule::importOraFileAsPrecomp()`

Important design note:

- `OraModule` depends on core types like `Canvas`, `Document`, `BoundingBox`, and `FilesHandler`
- core does not need to know ORA-specific policies

### 3. Compatibility Layer

Files:

- [evformat.h](/media/kinect/Project/friction-1.0.1/src/core/ReadWrite/evformat.h)
- [boundingbox.cpp](/media/kinect/Project/friction-1.0.1/src/core/Boxes/boundingbox.cpp)

What it does:

- keeps save compatibility at project format version `34`
- avoids writing the local-only timeline color field that caused old Friction to reject files

Why this matters:

- this is not a feature module
- this is a temporary compatibility shim so your modified build stays interoperable with upstream/opening older Friction

### 4. WEBM Alpha Policy Module

Module folder:

- [src/modules/webm](/media/kinect/Project/friction-1.0.1/src/modules/webm)

Public entry points:

- [webmalphapolicy.h](/media/kinect/Project/friction-1.0.1/src/modules/webm/webmalphapolicy.h)

Main implementation:

- [webmalphapolicy.cpp](/media/kinect/Project/friction-1.0.1/src/modules/webm/webmalphapolicy.cpp)

What this module owns:

- deciding when transparent-edge normalization should run
- applying premultiply correction for decoded RGBA frames

Thin host glue:

- [videoframeloader.cpp](/media/kinect/Project/friction-1.0.1/src/core/FileCacheHandlers/videoframeloader.cpp)

What the glue does now:

- decode the frame
- convert to RGBA
- call `WebmAlphaPolicy` if the imported file is a WebM with alpha

### 5. Puppet Tool Module

Module folder:

- [src/modules/puppet](/media/kinect/Project/friction-1.0.1/src/modules/puppet)

Public entry points:

- [puppettoolmodule.h](/media/kinect/Project/friction-1.0.1/src/modules/puppet/puppettoolmodule.h)

Main implementation:

- [puppettoolmodule.cpp](/media/kinect/Project/friction-1.0.1/src/modules/puppet/puppettoolmodule.cpp)

What this module owns:

- resolving the active puppet target
- finding or creating the host `PuppetEffect`
- converting canvas clicks into normalized pin positions
- adding a new pin from the tool layer

Thin host glue:

- [canvasmouseinteractions.cpp](/media/kinect/Project/friction-1.0.1/src/core/canvasmouseinteractions.cpp)

What the glue does now:

- decide whether the click should move an existing point or create a new pin
- show the status message when nothing is selected
- hand off actual pin creation to `PuppetToolModule`

## Remaining High-Risk Glue

These are still the most coupled files and should be treated as dispatcher layers only:

- [canvasmouseinteractions.cpp](/media/kinect/Project/friction-1.0.1/src/core/canvasmouseinteractions.cpp)
- [canvashandlesmartpath.cpp](/media/kinect/Project/friction-1.0.1/src/core/canvashandlesmartpath.cpp)

Why they are risky:

- tool input, selection, object creation, and feature-specific policy still meet here
- regressions here tend to look like random viewport/timeline behavior bugs

Safe rule:

- if a new feature is not generic canvas behavior, move its policy into `src/modules/...`

## Next Isolation Targets

### Puppet

Still available for future extraction:

- mesh preview policy
- remesh triggers
- toolbar integration helpers

Should stay in core:

- low-level raster effect host interfaces

### WEBM

Still available for future extraction:

- any extra WebM container/codec policy beyond alpha premultiplication

Should stay in core:

- generic video cache lifecycle

## If We Remove the Glue Layer

Short answer:

- removing the module calls from the thin host glue would remove the custom AE-like behaviors
- it would not automatically break base Friction core behavior

That means there is no full rewrite requirement yet.

The custom work is messy, but much of it is still recoverable because it lives near import/tool/effect boundaries instead of deep transform math.

## Practical Working Rule

Before any future feature change, answer these first:

1. What existing Friction host owns this feature
2. What single module file is the public entry point
3. If we delete that module folder and one registration call, does the feature disappear cleanly
