/*
#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#
*/

// Fork of enve - Copyright (C) 2016-2020 Maurycy Liebner

#ifndef BOXSCROLLWIDGET_H
#define BOXSCROLLWIDGET_H

#include "optimalscrollarena/scrollwidget.h"
#include "misc/keyfocustarget.h"
#include "conncontextptr.h"

#include <unordered_map>

class BoxScroller;
class ScrollArea;
class WindowSingleWidgetTarget;
class Document;
class Canvas;
class KeysView;
class TimelineHighlightWidget;
class BoundingBox;
class Property;
class SingleWidgetTarget;
class SWT_Abstraction;
class eBoxOrSound;

class BoxScrollWidget : public ScrollWidget {
    Q_OBJECT
public:
    enum class AeRevealPreset {
        None,
        AnchorPoint,
        Position,
        Scale,
        Rotation,
        Opacity,
        Keyframed,
        Modified,
        FrameRemapping,
        Masks
    };

    explicit BoxScrollWidget(Document& document,
                             ScrollArea * const parent = nullptr);
    ~BoxScrollWidget();

    void setCurrentScene(Canvas* const scene);
    void setSiblingKeysView(KeysView* const keysView);
    TimelineHighlightWidget *requestHighlighter();
    void applyAeRevealPreset(AeRevealPreset preset);
    void clearAeRevealPreset();
    void revealSelectedFrameRemapping();
    void revealSelectedMasks();
    AeRevealPreset currentAeRevealPreset() const { return mCurrentAeRevealPreset; }
    void toggleSolo(eBoxOrSound *target);
    bool isSolo(const eBoxOrSound *target) const;
private:
    BoxScroller *getBoxScroller();
    void restoreAeRevealState();
    void reapplyAeRevealPreset();
    void applyAeRevealPresetToBox(BoundingBox *box);
    void applyFrameRemappingRevealToBox(BoundingBox *box);
    void applyMaskRevealToBox(BoundingBox *box);
    void setTargetVisibleTracked(SingleWidgetTarget *target, bool visible);
    void setAbstractionExpandedTracked(SWT_Abstraction *abstraction, bool expanded);
    bool matchesRevealPreset(Property *property, AeRevealPreset preset) const;

    stdptr<SWT_Abstraction> mCoreAbs;
    Document &mDocument;
    std::unordered_map<SingleWidgetTarget*, bool> mStoredVisibility;
    std::unordered_map<SWT_Abstraction*, bool> mStoredExpanded;
    std::unordered_map<eBoxOrSound*, bool> mSoloVisibility;
    AeRevealPreset mCurrentAeRevealPreset = AeRevealPreset::None;
    eBoxOrSound *mSoloTarget = nullptr;
    bool mHasStoredRules = false;
    SWT_RulesCollection mStoredRules;
    ConnContextPtr<Canvas> mRevealScene;
};

#endif // BOXSCROLLWIDGET_H
