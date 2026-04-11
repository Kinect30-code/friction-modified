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

#ifndef BOXESLISTKEYSVIEWWIDGET_H
#define BOXESLISTKEYSVIEWWIDGET_H

#include <QWidget>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QDomElement>
#include <QPointer>

#include "smartPointers/stdselfref.h"
#include "framerange.h"
#include "widgets/fakemenubar.h"
#include "XML/runtimewriteid.h"
#include "GUI/BoxesList/boxscrollwidget.h"

#include "ReadWrite/ereadstream.h"
#include "ReadWrite/ewritestream.h"

class SWT_Abstraction;
class FrameScrollBar;
class KeysView;
class ChangeWidthWidget;
class MainWindow;
class ScrollArea;
class AnimationDockWidget;
class Document;
class Canvas;
class SceneChooser;
class StackWrapperCornerMenu;
class BoxScroller;
class XevReadBoxesHandler;
class BoundingBox;
class ContainerBox;

enum class SWT_Type : short;
enum class SWT_BoxRule : short;
enum class SWT_Target : short;

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(Document& document,
                            QWidget * const menu,
                            QWidget *parent);

    ~TimelineWidget();
    Canvas* getCurrrentScene() const {
        return mCurrentScene;
    }

    void setCurrentScene(Canvas* const scene);
    void setBoxesListWidth(const int width);
    void setGraphEnabled(const bool enabled);
    void applyAeRevealPreset(BoxScrollWidget::AeRevealPreset preset);
    void clearAeRevealPreset();
    void revealSelectedFrameRemapping();
    void revealSelectedMasks();
    void toggleSelectedTransformVisibility();
    void showSelectedKeyEaseMenu();
    void showSelectedKeyStrengthMenu();
    bool enterGroup(ContainerBox *group);
    bool exitCurrentGroup();
    void showGroupFlowPopup();
    void showAeTimelineContextMenu(const QPoint &globalPos);
    bool handleTimelineLayerSelection(BoundingBox *box, Qt::KeyboardModifiers modifiers);

    void writeState(eWriteStream& dst) const;
    void readState(eReadStream& src);

    void readStateXEV(XevReadBoxesHandler& boxReadHandler,
                      const QDomElement& ele,
                      RuntimeIdToWriteId& objListIdConv);
    void writeStateXEV(QDomElement& ele, QDomDocument& doc,
                       RuntimeIdToWriteId& objListIdConv) const;
    void readSettings(ChangeWidthWidget *chww);
    void writeSettings();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool handleAeShortcutEvent(QKeyEvent *event);
    void setViewedFrameRange(const FrameRange &range);
    void setCanvasFrameRange(const FrameRange &range);
    void ensureCurrentFrameVisible(int frame);
    BoundingBox *createSolidLayer();
    BoundingBox *createShapeLayer();
    BoundingBox *createTextLayer();
    BoundingBox *createNullLayer();
    void selectOnlyBox(BoundingBox *box);

    void setSearchText(const QString &text);
    void moveSlider(int val);
signals:
    void typeChanged(const SWT_Type target);
    void targetChanged(const SWT_Target target);
    void boxRuleChanged(const SWT_BoxRule rule);
private:
    void setType(const SWT_Type type);
    void setBoxRule(const SWT_BoxRule rule);
    void setTarget(const SWT_Target target);

    Canvas* mCurrentScene = nullptr;
    QPointer<BoundingBox> mTimelineSelectionAnchor;

    Document& mDocument;

    SceneChooser* mSceneChooser;

    FrameScrollBar* mFrameScrollBar;
    FrameScrollBar* mFrameRangeScrollBar;

    QGridLayout *mMainLayout;
    QVBoxLayout *mBoxesListLayout;
    QVBoxLayout *mKeysViewLayout;
    QHBoxLayout *mMenuLayout;
    QHBoxLayout *mMenuWidgetsLayout;
    QAction *mGraphAct = nullptr;
    QWidget* mMenuWidget;
    QWidget *mMenuWidgetsCont;
    FakeMenuBar *mBoxesListMenuBar;
    FakeMenuBar *mCornerMenuBar;
    QLineEdit *mSearchLine;
    ScrollArea *mBoxesListScrollArea;
    BoxScrollWidget *mBoxesListWidget;
    KeysView *mKeysView;
    AnimationDockWidget *mAnimationDockWidget;
    QWidget *mGroupFlowPopup = nullptr;
};

#endif // BOXESLISTKEYSVIEWWIDGET_H
