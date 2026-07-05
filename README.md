
# Friction2D Modified - After Effects Like

[](https://github.com/GhaniDz/friction-modified#friction-modified---like-after-effects)

This is a 2D animation software based on [Friction](https://github.com/friction2d/friction) with extensive modifications, aiming to create an After Effects alternative on the Linux platform.

**⚠️Important Note: This is not the official version of Friction. Please do not submit any issues to the original project maintainers!**

![Friction Modified Screenshot](src/Screenshot_2026-07-04_23-38-44.png)

## ✨ Main features of this version

[](https://github.com/GhaniDz/friction-modified#-%E6%9C%AC%E7%89%88%E6%9C%AC%E7%9A%84%E4%B8%BB%E8%A6%81%E5%8A%9F%E8%83%BD)

## 🆕 This update (2026-04-28)

[](https://github.com/GhaniDz/friction-modified#-%E6%9C%AC%E6%AC%A1%E6%9B%B4%E6%96%B0-2026-04-28)

### Preview caching system rewrite

[](https://github.com/GhaniDz/friction-modified#%E9%A2%84%E8%A7%88%E7%BC%93%E5%AD%98%E7%B3%BB%E7%BB%9F%E9%87%8D%E5%86%99)

-   **Fixed the issue of reversed cache direction** : Nested composition/ORA caches are no longer filled "from right to left," but strictly follow the ascending frame number order 0→1→2→3... Forward caching
-   **Fixed the issue of premature caching** : Removed `previewHasBufferedAhead`steady-state check, caching continues until all frames are complete, and no longer automatically pauses every 60 frames.
-   **Fixed the cache range shrinking issue** : `renderDataFinished`Medium-wide range cache entries are no longer replaced by narrow range entries, and statically synthesized green cache bars are filled all at once.
-   **Fixed rendering cursor rollback issue** : Removed `renderCursorAheadOfNeed`the logic that caused the cursor to violently pull back, ensuring consistent rendering direction.
-   **DI (Dependency Injection) caching architecture** : `HddCachableCacheHandler`Introduces a generation marking mechanism `mCacheGeneration`to support lazy eviction of old caches when scenarios change, avoiding full cleanup.
-   **Idle buffer span extended** : from 2s/1s to 5s/3s, pre-buffering more frames during pauses.

### Save/Load Repair

[](https://github.com/GhaniDz/friction-modified#%E4%BF%9D%E5%AD%98%E5%8A%A0%E8%BD%BD%E4%BF%AE%E5%A4%8D)

-   **Fixed a video import issue where only one frame remained after saving and reopening** : `VideoBox`/ `ImageSequenceBox`This function is not called again until the file data is asynchronously loaded `animationDataChanged()`, waiting for `reloaded`a signal to update the animation range.
-   **Fixed the issue with file naming** : `prp_sFixName()`Leading numbers will no longer be stripped, `-`  `.`valid characters such as will be retained, and filenames will no longer have the "N/A" prefix.

### Enhanced multi-select editing

[](https://github.com/GhaniDz/friction-modified#%E5%A4%9A%E9%80%89%E7%BC%96%E8%BE%91%E5%A2%9E%E5%BC%BA)

-   **Multi-selection box synchronized keyframes** : After selecting multiple boxes, adding/deleting keys to their properties will simultaneously apply the corresponding properties to all selected boxes (similar to After Effects).

### Tab key navigation rewritten

[](https://github.com/GhaniDz/friction-modified#tab%E9%94%AE%E5%AF%BC%E8%88%AA%E9%87%8D%E5%86%99)

-   **Merging scene chains and group hierarchies** : When multiple scenes are displayed, the tab pop-up simultaneously shows the scene path and the group hierarchy of the current scene, without losing context.
-   **Repairing ORA residual chains** : When returning from ORA synthesis to normal synthesis, the navigation chain correctly clears the ORA hierarchy, and incorrect hierarchy relationships are no longer displayed.
-   **The root node uses the actual composition name** : the hard-coded "Composition" is no longer displayed in place of the actual composition name.

### Project panel redesign

[](https://github.com/GhaniDz/friction-modified#project%E9%9D%A2%E6%9D%BF%E9%87%8D%E7%BB%84)

-   **System folder grouping** : Composites are automatically grouped into `Compositions`a folder, and source materials are automatically grouped into `Footage`a folder.
-   **ORA Package Preservation Hierarchy** : Imported asset packages in ORA retain their original package/compositions/asset hierarchy structure.

### Audio repair

[](https://github.com/GhaniDz/friction-modified#%E9%9F%B3%E9%A2%91%E4%BF%AE%E5%A4%8D)

-   **To fix the issue where audio continued to play after the track was cleared** : A call `eSound`was added to the destructor `removeSound`to ensure that audio stopped immediately when the box was deleted.

### Nested synthesis cache propagation

[](https://github.com/GhaniDz/friction-modified#%E5%B5%8C%E5%A5%97%E5%90%88%E6%88%90%E7%BC%93%E5%AD%98%E4%BC%A0%E6%92%AD)

-   **The StateChanged signal**`BoundingBox` is emitted when content changes . It listens for and propagates to the main canvas, ensuring that the main canvas cache is correctly invalidated after modifications to nested compositions `stateChanged`.`InternalLinkBox``planUpdate`

### Code Cleanup

[](https://github.com/GhaniDz/friction-modified#%E4%BB%A3%E7%A0%81%E6%B8%85%E7%90%86)

-   Clean up a large amount of redundant code and comments in `canvas.cpp`//`renderhandler.cpp``assetswidget.cpp`

### After Effects style interface

[](https://github.com/GhaniDz/friction-modified#ae%E9%A3%8E%E6%A0%BC%E7%95%8C%E9%9D%A2)

-   The redesigned UI layout mimics the workflow of After Effects.
-   More intuitive hierarchical management and timeline operation

### Video format support

[](https://github.com/GhaniDz/friction-modified#%E8%A7%86%E9%A2%91%E6%A0%BC%E5%BC%8F%E6%94%AF%E6%8C%81)

-   **WebM Import and Alpha Channel Support** - Correctly Processing WebM Videos with Alpha Channel Using the libvpx Decoder

### Image format support

[](https://github.com/GhaniDz/friction-modified#%E5%9B%BE%E5%83%8F%E6%A0%BC%E5%BC%8F%E6%94%AF%E6%8C%81)

-   **ORA composition import** - Supports importing OpenRaster format compositions and supports hot updates.

### Hierarchical relationship system

[](https://github.com/GhaniDz/friction-modified#%E5%B1%82%E7%BA%A7%E5%85%B3%E7%B3%BB%E7%B3%BB%E7%BB%9F)

-   **Parent-child relationship** - A complete parent-child binding system
-   **Whip Connections** - Quickly establish hierarchical relationships using the Whip tool.

### Masks and Masks

[](https://github.com/GhaniDz/friction-modified#%E8%92%99%E7%89%88%E4%B8%8E%E9%81%AE%E7%BD%A9)

-   **AE Track Masking** - Supports track masking functionality similar to After Effects.
-   **AE Layer Mask** - You can create a mask directly using the Pen/Rectangle/Ellipse tools when the track is selected.

### Synthesis Management

[](https://github.com/GhaniDz/friction-modified#%E5%90%88%E6%88%90%E7%AE%A1%E7%90%86)

-   **Scene switching** - Quickly switch scenes like switching compositions in After Effects.

### Animation tools

[](https://github.com/GhaniDz/friction-modified#%E5%8A%A8%E7%94%BB%E5%B7%A5%E5%85%B7)

-   **After Effects Puppet Functionality** - Added puppet tools for character animation (may have stability issues, but is generally usable).

### Keyboard shortcut optimization

[](https://github.com/GhaniDz/friction-modified#%E5%BF%AB%E6%8D%B7%E9%94%AE%E4%BC%98%E5%8C%96)

-   A series of After Effects-like keyboard shortcuts have been added.
-   The Mark shortcut key has been changed from the M key to the numeric keypad * key.

## ⚠️Disclaimer

[](https://github.com/GhaniDz/friction-modified#%EF%B8%8F-%E5%85%8D%E8%B4%A3%E5%A3%B0%E6%98%8E)

**The code for this project is entirely generated by AI and is in a "black box" state, which may indicate that the core code of Friction has been deeply modified.**

-   Please do not submit issues for this version to the official Friction authors.
-   You are welcome to submit issues, but please include detailed error information.
-   Other developers are welcome to participate in maintenance and optimization.

## 📋 Original Project Information

[](https://github.com/GhaniDz/friction-modified#-%E5%8E%9F%E9%A1%B9%E7%9B%AE%E4%BF%A1%E6%81%AF)

This revised version is based on:

-   **Original project** : [Friction](https://friction.graphics/)
-   **Original author** : Ole-André Rodlie and contributors
-   **Original project on GitHub** : [https://github.com/friction2d/friction](https://github.com/friction2d/friction)

## 📖 Build Instructions

[](https://github.com/GhaniDz/friction-modified#-%E6%9E%84%E5%BB%BA%E8%AF%B4%E6%98%8E)

-   [Linux](https://friction.graphics/documentation/source-linux.html)
-   [Windows](https://friction.graphics/documentation/source-windows.html)
-   [macOS](https://friction.graphics/documentation/source-macos.html)

## 📄 License

[](https://github.com/GhaniDz/friction-modified#-%E8%AE%B8%E5%8F%AF%E8%AF%81)

This project retains the same license as the original project:

Friction is copyright © Ole-André Rodlie and contributors.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.

**This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the [GNU General Public License](https://github.com/GhaniDz/friction-modified/blob/main/LICENSE.md) for more details.**

Friction is based on [enve](https://github.com/MaurycyLiebner/enve) - Copyright © Maurycy Liebner and contributors.

Third-party software may contain other OSS licenses, see 'Help' > 'About' > 'Licenses' in Friction.

Source code for third-party software can be downloaded [here](https://download.friction.graphics/distfiles/).
