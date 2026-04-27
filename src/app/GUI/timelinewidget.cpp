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

#include <QToolButton>
#include <QStackedLayout>
#include <QDesktopWidget>
#include <QStatusBar>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QKeyEvent>
#include <QShortcut>
#include <QFrame>
#include <QCursor>
#include <QSignalBlocker>
#include <QSet>

#include "timelinewidget.h"
#include "widgets/framescrollbar.h"
#include "mainwindow.h"
#include "timelinedockwidget.h"
#include "GUI/BoxesList/boxsinglewidget.h"
#include "swt_abstraction.h"
#include "GUI/BoxesList/boxscrollwidget.h"
#include "keysview.h"
#include "GUI/BoxesList/boxscrollarea.h"
#include "GUI/BoxesList/boxscroller.h"
#include "canvaswindow.h"
#include "animationdockwidget.h"
#include "GUI/global.h"
#include "canvas.h"
#include "widgets/scenechooser.h"
#include "widgets/changewidthwidget.h"
#include "timelinehighlightwidget.h"
#include "themesupport.h"
#include "GUI/dialogsinterface.h"
#include "Private/esettings.h"
#include "Boxes/rectangle.h"
#include "Boxes/textbox.h"
#include "Boxes/nullobject.h"
#include "Boxes/containerbox.h"
#include "paintsettings.h"

namespace {

void activateSceneWorkspace(Document &document, Canvas *scene)
{
    if (!scene) { return; }
    if (auto *window = MainWindow::sGetInstance()) {
        window->activateSceneWorkspace(scene);
    } else {
        document.setActiveScene(scene);
    }
}

}

TimelineWidget::TimelineWidget(Document &document,
                               QWidget * const menu,
                               QWidget *parent)
    : QWidget(parent)
    , mDocument(document)
{
    setObjectName("AeTimelineWidget");
    setPalette(ThemeSupport::getDarkerPalette());
    setAutoFillBackground(true);
    setFocusPolicy(Qt::StrongFocus);

    mMainLayout = new QGridLayout(this);
    mMainLayout->setSpacing(0);
    mMainLayout->setMargin(0);
    mMainLayout->setColumnStretch(0, 32);
    mMainLayout->setColumnStretch(1, 68);
    mMainLayout->setRowStretch(1, 1);

    mMenuLayout = new QHBoxLayout();
    mMenuLayout->setSpacing(6);
    mMenuLayout->setMargin(0);

    mBoxesListMenuBar = new FakeMenuBar(this);
    mBoxesListMenuBar->setObjectName("AeTimelineLeftHeader");
    mBoxesListMenuBar->setProperty("aeHeader", true);
    mBoxesListMenuBar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    mSceneChooser = new SceneChooser(mDocument, true,
                                     mBoxesListMenuBar);
    mBoxesListMenuBar->addMenu(mSceneChooser);
    mSceneChooser->menuAction()->setVisible(false);
    mSceneChooser->menuAction()->setEnabled(false);

    mCornerMenuBar = new FakeMenuBar(this);
    mCornerMenuBar->setObjectName("TimelineMenu");
    mCornerMenuBar->setProperty("aeHeader", true);
    menu->setObjectName("TimelineMenu");
    menu->setProperty("aeHeader", true);

    const QIcon filterIcon = QIcon::fromTheme("filter");

    QMenu * const settingsMenu = mCornerMenuBar->addMenu(filterIcon, tr("Layer Filters"));
    QMenu * const objectsMenu = settingsMenu->addMenu(filterIcon, tr("Visibility"));

    const auto ruleActionAdder = [this, objectsMenu](
            const SWT_BoxRule rule, const QString& text) {
        const auto slot = [this, rule]() { setBoxRule(rule); };
        const auto action = objectsMenu->addAction(text, this, slot);
        action->setCheckable(true);
        connect(this, &TimelineWidget::boxRuleChanged,
                action, [action, rule](const SWT_BoxRule setRule) {
            action->setChecked(rule == setRule);
        });
        return action;
    };

    ruleActionAdder(SWT_BoxRule::all, "All")->setChecked(true);
    ruleActionAdder(SWT_BoxRule::selected, "Selected");
    ruleActionAdder(SWT_BoxRule::animated, "Animated");
    ruleActionAdder(SWT_BoxRule::notAnimated, "Not Animated");
    ruleActionAdder(SWT_BoxRule::visible, "Visible");
    ruleActionAdder(SWT_BoxRule::hidden, "Hidden");
    ruleActionAdder(SWT_BoxRule::unlocked, "Unlocked");
    ruleActionAdder(SWT_BoxRule::locked, "Locked");

    QMenu * const targetMenu = settingsMenu->addMenu(filterIcon, tr("Scope"));

    const auto targetActionAdder = [this, targetMenu](
            const SWT_Target target, const QString& text) {
        const auto slot = [this, target]() { setTarget(target); };
        const auto action = targetMenu->addAction(text, this, slot);
        action->setCheckable(true);
        connect(this, &TimelineWidget::targetChanged,
                action, [action, target](const SWT_Target setTarget) {
            action->setChecked(target == setTarget);
        });
        return action;
    };

    //targetActionAdder(SWT_Target::all, "All");
    targetActionAdder(SWT_Target::canvas, tr("Active Composition"))->setChecked(true);
    targetActionAdder(SWT_Target::group, tr("Active Group"));

    QMenu * const typeMenu = settingsMenu->addMenu(filterIcon, tr("Media"));

    const auto typeActionAdder = [this, typeMenu](
            const SWT_Type type, const QString& text) {
        const auto slot = [this, type]() { setType(type); };
        const auto action = typeMenu->addAction(text, this, slot);
        action->setCheckable(true);
        connect(this, &TimelineWidget::typeChanged,
                action, [action, type](const SWT_Type setType) {
            action->setChecked(type == setType);
        });
        return action;
    };

    typeActionAdder(SWT_Type::all, "All")->setChecked(true);
    typeActionAdder(SWT_Type::sound, "Sound");
    typeActionAdder(SWT_Type::graphics, "Graphics");

    settingsMenu->addSeparator();

    {
        const auto op = [this]() {
            setBoxRule(SWT_BoxRule::all);
            setTarget(SWT_Target::canvas);
            setType(SWT_Type::all);
        };
        const auto act = settingsMenu->addAction(tr("Clear Filters"), this, op);
        const auto can = [this]() {
            const auto rules = mBoxesListWidget->getRulesCollection();
            return rules.fRule != SWT_BoxRule::all ||
                   rules.fTarget != SWT_Target::canvas ||
                   rules.fType != SWT_Type::all;
        };
        const auto setEnabled = [act, can]() { act->setEnabled(can()); };
        connect(this, &TimelineWidget::typeChanged, act, setEnabled);
        connect(this, &TimelineWidget::targetChanged, act, setEnabled);
        connect(this, &TimelineWidget::boxRuleChanged, act, setEnabled);
    }

    //QMenu *viewMenu = mBoxesListMenuBar->addMenu("View");
    mGraphAct = mCornerMenuBar->addAction(QIcon::fromTheme("graph"),
                                          tr("Graph Editor"));
    mGraphAct->setCheckable(true);
    mGraphAct->setToolTip(tr("Show or hide the graph editor"));
    connect(mGraphAct, &QAction::toggled,
            this, &TimelineWidget::setGraphEnabled);

    //mCornerMenuBar->setContentsMargins(0, 0, 1, 0);

    mSearchLine = new QLineEdit("", mBoxesListMenuBar);
    mSearchLine->setObjectName("SearchLine");
    mSearchLine->setMinimumHeight(0);
    mSearchLine->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    MainWindow::sGetInstance()->installLineFilter(mSearchLine);
    mSearchLine->setPlaceholderText(tr("Search Layers"));
    connect(mSearchLine, &QLineEdit::textChanged,
            this, &TimelineWidget::setSearchText);
    mSearchLine->setFocusPolicy(Qt::ClickFocus);
    mSearchLine->installEventFilter(this);

    mMenuWidget = new QWidget(this);
    mMenuWidget->setObjectName("AeTimelineHeader");
    mMenuWidget->setLayout(mMenuLayout);

    mMenuLayout->addWidget(mBoxesListMenuBar);
    mMenuLayout->addWidget(mSearchLine);
    mMenuLayout->addWidget(mCornerMenuBar);
    mMenuLayout->addWidget(menu);

    mBoxesListScrollArea = new ScrollArea(this);
    mBoxesListScrollArea->setObjectName("AeTimelineLayersScroll");
    mBoxesListScrollArea->setFocusPolicy(Qt::StrongFocus);
    mBoxesListScrollArea->installEventFilter(this);

    mBoxesListWidget = new BoxScrollWidget(mDocument, mBoxesListScrollArea);
    mBoxesListWidget->setObjectName("AeTimelineLayers");
    mBoxesListWidget->setFocusPolicy(Qt::StrongFocus);
    mBoxesListWidget->setCurrentRule(SWT_BoxRule::all);
    mBoxesListWidget->setCurrentTarget(nullptr, SWT_Target::canvas);
    mBoxesListWidget->installEventFilter(this);

    mBoxesListScrollArea->setWidget(mBoxesListWidget);
    mMainLayout->addWidget(mMenuWidget, 0, 0, Qt::AlignTop);
    mMainLayout->addWidget(mBoxesListScrollArea, 1, 0);

    mKeysViewLayout = new QVBoxLayout();
    mKeysViewLayout->setContentsMargins(0, 0, 0, 0);
    mKeysViewLayout->setSpacing(0);

    mKeysView = new KeysView(mBoxesListWidget, this);
    mKeysView->setObjectName("AeTimelineKeysView");
    mKeysView->setFocusPolicy(Qt::StrongFocus);
    mKeysView->installEventFilter(this);
    mKeysViewLayout->addWidget(mKeysView);

    connect(mKeysView, &KeysView::statusMessage,
            this, [](const QString &message) {
        if (MainWindow::sGetInstance()->statusBar()) {
            MainWindow::sGetInstance()->statusBar()->showMessage(message, 5000);
        }
    });

    const auto high1 = mBoxesListWidget->requestHighlighter();
    const auto high2 = mKeysView->requestHighlighter();
    high1->setOther(high2);
    high2->setOther(high1);

    mAnimationDockWidget = new AnimationDockWidget(this, mKeysView);
    mAnimationDockWidget->showGraph(false);
    mMainLayout->addLayout(mKeysViewLayout, 1, 1);

    const auto keysViewScrollbarLayout = new QHBoxLayout();
    const auto layoutT = new QVBoxLayout();
    layoutT->setAlignment(Qt::AlignBottom);
    layoutT->addWidget(mAnimationDockWidget);
    keysViewScrollbarLayout->addLayout(layoutT);
    mKeysView->setLayout(keysViewScrollbarLayout);
    keysViewScrollbarLayout->setAlignment(Qt::AlignRight);
    /*keysViewScrollbarLayout->addWidget(
                mBoxesListScrollArea->verticalScrollBar());*/
    mBoxesListScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    keysViewScrollbarLayout->setContentsMargins(0, 0, 0, 0);

    connect(mBoxesListScrollArea->verticalScrollBar(),
            &QScrollBar::valueChanged,
            mBoxesListWidget, &BoxScrollWidget::changeVisibleTop);
    connect(mBoxesListScrollArea, &ScrollArea::heightChanged,
            mBoxesListWidget, &BoxScrollWidget::changeVisibleHeight);
    connect(mBoxesListScrollArea, &ScrollArea::widthChanged,
            mBoxesListWidget, &BoxScrollWidget::setWidth);

    connect(mBoxesListScrollArea->verticalScrollBar(),
            &QScrollBar::valueChanged,
            this, &TimelineWidget::moveSlider);
    connect(mKeysView, &KeysView::wheelEventSignal,
            mBoxesListScrollArea, &ScrollArea::callWheelEvent);
    connect(mKeysView, &KeysView::changedViewedFrames,
             this, &TimelineWidget::setViewedFrameRange);

    connect(mSceneChooser, &SceneChooser::currentChanged,
            this, [this](Canvas * const scene) {
        setCurrentScene(scene);
        activateSceneWorkspace(mDocument, scene);
    });

    eSizesUI::widget.add(mBoxesListScrollArea, [this](const int size) {
        mBoxesListScrollArea->setFixedWidth(20*size);
    });

    setLayout(mMainLayout);

    mFrameScrollBar = new FrameScrollBar(1, 1, false, false, false, this);
    mFrameScrollBar->setObjectName("AeTimelineRuler");
    mFrameScrollBar->setSizePolicy(QSizePolicy::Minimum,
                                   QSizePolicy::Preferred);

    const qreal dpi = QApplication::desktop()->logicalDpiX() / 96.0;
    mFrameScrollBar->setFixedHeight(40 * dpi);

//    connect(MemoryHandler::sGetInstance(), &MemoryHandler::memoryFreed,
//            frameScrollBar,
//            qOverload<>(&FrameScrollBar::update));
    connect(mFrameScrollBar, &FrameScrollBar::triggeredFrameRangeChange,
            this, [this](const FrameRange& range){
        if (!mSceneChooser->getCurrentScene()) { return; }
        Document::sInstance->setActiveSceneFrame(range.fMin);
    });
    mMainLayout->addWidget(mFrameScrollBar, 0, 1);

    mFrameRangeScrollBar = new FrameScrollBar(20, 200, true, true, true, this);
    mFrameRangeScrollBar->setObjectName("AeTimelineNavigator");
    mFrameRangeScrollBar->setFixedHeight(10);
    //eSizesUI::widget.add(mFrameRangeScrollBar, [this](const int size) {
    //    mFrameRangeScrollBar->setMinimumHeight(size+5/**2/3*/);
    //});

    connect(mFrameRangeScrollBar, &FrameScrollBar::triggeredFrameRangeChange,
            this, &TimelineWidget::setViewedFrameRange);
    connect(mKeysView, &KeysView::wheelEventSignal,
            mFrameRangeScrollBar, &FrameScrollBar::callWheelEvent);

#ifdef Q_OS_MAC
    connect(mKeysView, &KeysView::panEventSignal,
            mFrameRangeScrollBar, &FrameScrollBar::callPanEvent);
    connect(mKeysView, &KeysView::nativeEventSignal,
            mFrameRangeScrollBar, &FrameScrollBar::callNativeGestures);
#endif

    mKeysViewLayout->addWidget(mFrameRangeScrollBar);
    //mSceneChooser->setCurrentScene(mDocument.fActiveScene); // why?


    const auto chww = new ChangeWidthWidget(this);
    chww->show();
    chww->updatePos();
    chww->raise();
    connect(chww, &ChangeWidthWidget::widthSet,
            this, &TimelineWidget::setBoxesListWidth);

    readSettings(chww);
    installEventFilter(this);

    const auto addTimelineShortcut = [this](const QKeySequence &seq,
                                            const std::function<void()> &fn) {
        auto *shortcut = new QShortcut(seq, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, fn]() {
            const QRect globalRect(mapToGlobal(QPoint(0, 0)), size());
            if (!globalRect.contains(QCursor::pos()) && !isAncestorOf(QApplication::focusWidget())) {
                return;
            }
            fn();
        });
    };
    addTimelineShortcut(QKeySequence(Qt::CTRL + Qt::Key_Tab), [this]() {
        if (mGraphAct) {
            mGraphAct->trigger();
        }
    });
    addTimelineShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Tab), [this]() {
        if (mGraphAct) {
            mGraphAct->trigger();
        }
    });
    addTimelineShortcut(QKeySequence(Qt::Key_Tab), [this]() {
        showGroupFlowPopup();
    });
    addTimelineShortcut(QKeySequence(Qt::SHIFT + Qt::Key_Tab), [this]() {
        showGroupFlowPopup();
    });
}

TimelineWidget::~TimelineWidget()
{
    writeSettings();
}

bool TimelineWidget::handleAeShortcutEvent(QKeyEvent *event)
{
    if (!event) { return false; }
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }

    switch (event->key()) {
    case Qt::Key_Tab:
        showGroupFlowPopup();
        return true;
    default:
        return false;
    }
}

void TimelineWidget::refreshTimelineFromSceneUpdate(const bool interactivePreview)
{
    if(interactivePreview) {
        scheduleInteractivePlaybackRefresh();
        return;
    }

    mFrameScrollBar->update();
    mFrameRangeScrollBar->update();
    mKeysView->update();
    mBoxesListWidget->updateVisible();
}

void TimelineWidget::scheduleInteractivePlaybackRefresh()
{
    if(!mPlaybackUiRefreshQueued) {
        mPlaybackUiRefreshQueued = true;
        QTimer::singleShot(16, this, [this]() {
            mPlaybackUiRefreshQueued = false;
            mFrameScrollBar->update();
            mFrameRangeScrollBar->update();
            mKeysView->update();
        });
    }

    if(!mPlaybackHeavyUiRefreshQueued) {
        mPlaybackHeavyUiRefreshQueued = true;
        QTimer::singleShot(120, this, [this]() {
            mPlaybackHeavyUiRefreshQueued = false;
            if(mCurrentScene && mCurrentScene->isPreviewingOrRendering()) {
                mBoxesListWidget->updateVisible();
            }
        });
    }
}

bool TimelineWidget::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)
    if (!event) { return QWidget::eventFilter(watched, event); }
    if (event->type() == QEvent::ShortcutOverride) {
        auto *keyEvent = static_cast<QKeyEvent*>(event);
        if (handleAeShortcutEvent(keyEvent)) {
            event->accept();
            return true;
        }
    } else if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent*>(event);
        if (handleAeShortcutEvent(keyEvent)) {
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TimelineWidget::selectOnlyBox(BoundingBox *box)
{
    if (!mCurrentScene || !box) { return; }
    mCurrentScene->clearBoxesSelection();
    mCurrentScene->addBoxToSelection(box);
    mTimelineSelectionAnchor = box;
}

QList<BoundingBox*> TimelineWidget::displayedTimelineLayerBoxes() const
{
    if (!mBoxesListWidget) {
        return {};
    }

    auto *mainAbstraction = mBoxesListWidget->getMainAbstration();
    if (!mainAbstraction) {
        return mCurrentScene ? mCurrentScene->getContainedBoxes() : QList<BoundingBox*>{};
    }

    QList<BoundingBox*> boxes;
    QSet<BoundingBox*> seenBoxes;
    const auto rules = mBoxesListWidget->getRulesCollection();
    const int rowHeight = eSizesUI::widget;
    int currY = rowHeight/2;
    const int maxY = qMax(mBoxesListWidget->getContentHeight(), rowHeight) +
                     rowHeight;
    const SetAbsFunc collectBox = [&boxes, &seenBoxes](
            SWT_Abstraction *abs, const int) {
        if (!abs) { return; }
        auto *box = enve_cast<BoundingBox*>(abs->getTarget());
        if (!box || seenBoxes.contains(box)) { return; }
        seenBoxes.insert(box);
        boxes.append(box);
    };
    mainAbstraction->setAbstractions(-1, maxY,
                                     currY, 0, rowHeight,
                                     collectBox, rules,
                                     true, false);

    if (boxes.isEmpty() && mCurrentScene) {
        return mCurrentScene->getContainedBoxes();
    }
    return boxes;
}

bool TimelineWidget::handleTimelineLayerSelection(BoundingBox *box,
                                                  Qt::KeyboardModifiers modifiers)
{
    if (!mCurrentScene || !box) { return false; }

    const bool shift = modifiers & Qt::ShiftModifier;
    const bool ctrl = modifiers & Qt::ControlModifier;
    if (!shift) {
        if (ctrl) {
            if (box->isSelected()) {
                mCurrentScene->removeBoxFromSelection(box);
            } else {
                mCurrentScene->clearSelectedProps();
                mCurrentScene->addBoxToSelection(box);
            }
        } else {
            mCurrentScene->clearSelectedProps();
            mCurrentScene->clearBoxesSelection();
            mCurrentScene->addBoxToSelection(box);
        }
        mTimelineSelectionAnchor = box;
        return true;
    }

    BoundingBox *anchor = mTimelineSelectionAnchor;
    if (!anchor) {
        const auto selected = mCurrentScene->selectedBoxesList();
        if (!selected.isEmpty()) {
            anchor = selected.last();
        }
    }
    if (!anchor) {
        mCurrentScene->clearSelectedProps();
        mCurrentScene->addBoxToSelection(box);
        mTimelineSelectionAnchor = box;
        return true;
    }

    const auto boxes = displayedTimelineLayerBoxes();
    if (boxes.isEmpty()) {
        mCurrentScene->clearSelectedProps();
        mCurrentScene->addBoxToSelection(box);
        mTimelineSelectionAnchor = box;
        return true;
    }

    auto indexForBox = [&boxes](BoundingBox *candidate) {
        return candidate ? boxes.indexOf(candidate) : -1;
    };

    int anchorIndex = indexForBox(anchor);
    if (anchorIndex < 0) {
        const auto selected = mCurrentScene->selectedBoxesList();
        for (int i = selected.count() - 1; i >= 0; --i) {
            anchor = selected.at(i);
            anchorIndex = indexForBox(anchor);
            if (anchorIndex >= 0) {
                break;
            }
        }
    }
    const int boxIndex = indexForBox(box);
    if (anchorIndex < 0 || boxIndex < 0) {
        mCurrentScene->clearSelectedProps();
        mCurrentScene->addBoxToSelection(box);
        mTimelineSelectionAnchor = box;
        return true;
    }

    if (!ctrl) {
        mCurrentScene->clearSelectedProps();
        mCurrentScene->clearBoxesSelection();
    }
    const int minIndex = qMin(anchorIndex, boxIndex);
    const int maxIndex = qMax(anchorIndex, boxIndex);
    for (int i = minIndex; i <= maxIndex; ++i) {
        if (auto *candidate = boxes.at(i)) {
            mCurrentScene->addBoxToSelection(candidate);
        }
    }
    return true;
}

bool TimelineWidget::handleTimelineLayerRectSelection(
        const QList<BoundingBox *> &boxes,
        Qt::KeyboardModifiers modifiers)
{
    if (!mCurrentScene) { return false; }

    QList<BoundingBox*> uniqueBoxes;
    QSet<BoundingBox*> seenBoxes;
    for (auto *box : boxes) {
        if (!box || seenBoxes.contains(box)) {
            continue;
        }
        seenBoxes.insert(box);
        uniqueBoxes.append(box);
    }
    if (uniqueBoxes.isEmpty()) {
        return false;
    }

    const bool extendSelection = modifiers & (Qt::ShiftModifier | Qt::ControlModifier);
    mCurrentScene->clearSelectedProps();
    if (!extendSelection) {
        mCurrentScene->clearBoxesSelection();
    }
    for (auto *box : uniqueBoxes) {
        mCurrentScene->addBoxToSelection(box);
    }
    mTimelineSelectionAnchor = uniqueBoxes.last();
    return true;
}

BoundingBox *TimelineWidget::createSolidLayer()
{
    if (!mCurrentScene) { return nullptr; }
    activateSceneWorkspace(mDocument, mCurrentScene);
    auto *group = mCurrentScene->getCurrentGroup();
    if (!group) { group = mCurrentScene; }

    const int width = mCurrentScene->getCanvasWidth();
    const int height = mCurrentScene->getCanvasHeight();
    const auto solid = enve::make_shared<RectangleBox>();
    solid->prp_setName(tr("Solid"));
    solid->setTopLeftPos(QPointF(-width/2.0, -height/2.0));
    solid->setBottomRightPos(QPointF(width/2.0, height/2.0));
    if (auto *fill = solid->getFillSettings()) {
        fill->setPaintType(PaintType::FLATPAINT);
        fill->getColorAnimator()->setColor(eSettings::instance().fLastUsedFillColor);
    }
    if (auto *stroke = solid->getStrokeSettings()) {
        stroke->setPaintType(PaintType::NOPAINT);
    }
    group->addContained(solid);
    solid->setAbsolutePos(QPointF(width/2.0, height/2.0));
    solid->planCenterPivotPosition();
    return solid.get();
}

BoundingBox *TimelineWidget::createShapeLayer()
{
    if (!mCurrentScene) { return nullptr; }
    activateSceneWorkspace(mDocument, mCurrentScene);
    auto *group = mCurrentScene->getCurrentGroup();
    if (!group) { group = mCurrentScene; }

    const int width = qMax(240, mCurrentScene->getCanvasWidth()/4);
    const int height = qMax(240, mCurrentScene->getCanvasHeight()/4);
    const auto shapeLayer = enve::make_shared<ContainerBox>(QStringLiteral("Shape Layer"),
                                                            eBoxType::layer);
    const auto shape = enve::make_shared<RectangleBox>();
    shape->prp_setName(tr("Rectangle"));
    shape->setTopLeftPos(QPointF(-width/2.0, -height/2.0));
    shape->setBottomRightPos(QPointF(width/2.0, height/2.0));
    if (auto *fill = shape->getFillSettings()) {
        fill->setPaintType(PaintType::FLATPAINT);
        fill->getColorAnimator()->setColor(eSettings::instance().fLastUsedFillColor);
    }
    if (auto *stroke = shape->getStrokeSettings()) {
        stroke->setPaintType(PaintType::NOPAINT);
    }
    shapeLayer->addContained(shape);
    group->addContained(shapeLayer);
    shapeLayer->setAbsolutePos(QPointF(mCurrentScene->getCanvasWidth()/2.0,
                                       mCurrentScene->getCanvasHeight()/2.0));
    shapeLayer->planCenterPivotPosition();
    return shapeLayer.get();
}

BoundingBox *TimelineWidget::createTextLayer()
{
    if (!mCurrentScene) { return nullptr; }
    activateSceneWorkspace(mDocument, mCurrentScene);
    auto *group = mCurrentScene->getCurrentGroup();
    if (!group) { group = mCurrentScene; }

    const auto text = enve::make_shared<TextBox>();
    text->prp_setName(tr("Text Layer"));
    text->setFontFamilyAndStyle(mDocument.fFontFamily, mDocument.fFontStyle);
    text->setFontSize(mDocument.fFontSize);
    group->addContained(text);
    text->setAbsolutePos(QPointF(mCurrentScene->getCanvasWidth()/2.0,
                                 mCurrentScene->getCanvasHeight()/2.0));
    text->planCenterPivotPosition();
    return text.get();
}

BoundingBox *TimelineWidget::createNullLayer()
{
    if (!mCurrentScene) { return nullptr; }
    activateSceneWorkspace(mDocument, mCurrentScene);
    auto *group = mCurrentScene->getCurrentGroup();
    if (!group) { group = mCurrentScene; }

    const auto nullObject = enve::make_shared<NullObject>();
    nullObject->prp_setName(tr("Null Object"));
    group->addContained(nullObject);
    nullObject->setAbsolutePos(QPointF(mCurrentScene->getCanvasWidth()/2.0,
                                       mCurrentScene->getCanvasHeight()/2.0));
    nullObject->planCenterPivotPosition();
    return nullObject.get();
}

void TimelineWidget::showAeTimelineContextMenu(const QPoint &globalPos)
{
    enum class PendingAction {
        None,
        NewSolid,
        NewShape,
        NewText,
        NewNull,
        CompositionSettings
    };

    QMenu menu(this);
    auto *newMenu = menu.addMenu(QIcon::fromTheme("list-add"), tr("New"));
    auto *solidAct = newMenu->addAction(QIcon::fromTheme("layer"), tr("New Solid Layer"));
    auto *shapeAct = newMenu->addAction(QIcon::fromTheme("shape-rectangle"), tr("New Shape Layer"));
    auto *textAct = newMenu->addAction(QIcon::fromTheme("draw-text"), tr("New Text Layer"));
    auto *nullAct = newMenu->addAction(QIcon::fromTheme("crosshairs"), tr("New Null Object"));

    menu.addSeparator();
    auto *compSettingsAct = menu.addAction(QIcon::fromTheme("sequence"),
                                           tr("Composition Settings"));

    const auto selectedAction = menu.exec(globalPos);
    if (!selectedAction) { return; }

    PendingAction pendingAction = PendingAction::None;
    if (selectedAction == solidAct) {
        pendingAction = PendingAction::NewSolid;
    } else if (selectedAction == shapeAct) {
        pendingAction = PendingAction::NewShape;
    } else if (selectedAction == textAct) {
        pendingAction = PendingAction::NewText;
    } else if (selectedAction == nullAct) {
        pendingAction = PendingAction::NewNull;
    } else if (selectedAction == compSettingsAct) {
        pendingAction = PendingAction::CompositionSettings;
    }

    if (pendingAction == PendingAction::None) { return; }

    QPointer<TimelineWidget> that(this);
    QPointer<Canvas> scene = mCurrentScene;
    QTimer::singleShot(0, this, [that, scene, pendingAction]() {
        if (!that) { return; }
        BoundingBox *createdBox = nullptr;
        switch (pendingAction) {
        case PendingAction::NewSolid:
            createdBox = that->createSolidLayer();
            break;
        case PendingAction::NewShape:
            createdBox = that->createShapeLayer();
            break;
        case PendingAction::NewText:
            createdBox = that->createTextLayer();
            break;
        case PendingAction::NewNull:
            createdBox = that->createNullLayer();
            break;
        case PendingAction::CompositionSettings:
            if (scene) {
                DialogsInterface::instance().showSceneSettingsDialog(scene);
            }
            break;
        case PendingAction::None:
            break;
        }

        if (!that || !scene || !createdBox) { return; }
        QPointer<BoundingBox> createdBoxPtr(createdBox);
        QTimer::singleShot(0, that, [that, scene, createdBoxPtr, pendingAction]() {
            if (!that || !scene || !createdBoxPtr) { return; }
            that->selectOnlyBox(createdBoxPtr);
            Document::sInstance->actionFinished();
            if (pendingAction == PendingAction::NewText) {
                emit scene->openTextEditor();
            }
        });
    });
}

void TimelineWidget::applyAeRevealPreset(BoxScrollWidget::AeRevealPreset preset)
{
    if (mBoxesListWidget) {
        mBoxesListWidget->applyAeRevealPreset(preset);
    }
}

void TimelineWidget::clearAeRevealPreset()
{
    if (mBoxesListWidget) {
        mBoxesListWidget->clearAeRevealPreset();
    }
}

void TimelineWidget::showSelectedKeyEaseMenu()
{
    if (mKeysView) {
        mKeysView->showQuickEaseMenu();
    }
}

void TimelineWidget::showSelectedKeyStrengthMenu()
{
    if (mKeysView) {
        mKeysView->showQuickStrengthMenu();
    }
}

void TimelineWidget::toggleSelectedTransformVisibility()
{
    if (mBoxesListWidget) {
        mBoxesListWidget->toggleSelectedTransformVisibility();
    }
}

void TimelineWidget::revealSelectedFrameRemapping()
{
    if (mBoxesListWidget) {
        mBoxesListWidget->revealSelectedFrameRemapping();
    }
}

void TimelineWidget::revealSelectedMasks()
{
    if (mBoxesListWidget) {
        mBoxesListWidget->revealSelectedMasks();
    }
}

bool TimelineWidget::enterGroup(ContainerBox *group)
{
    if (!mCurrentScene || !group) {
        return false;
    }

    if (group == mCurrentScene) {
        setTarget(SWT_Target::canvas);
        return true;
    }

    if (group->getParentScene() != mCurrentScene) {
        return false;
    }

    mCurrentScene->setCurrentBoxesGroup(group);
    clearAeRevealPreset();
    setTarget(SWT_Target::group);
    return true;
}

bool TimelineWidget::exitCurrentGroup()
{
    if (!mCurrentScene) {
        return false;
    }

    auto *group = mCurrentScene->getCurrentGroup();
    if (!group || group == mCurrentScene) {
        setTarget(SWT_Target::canvas);
        return false;
    }

    if (group->getParentGroup()) {
        mCurrentScene->setCurrentGroupParentAsCurrentGroup();
        clearAeRevealPreset();
        setTarget(SWT_Target::group);
    } else {
        setTarget(SWT_Target::canvas);
    }
    return true;
}

void TimelineWidget::showGroupFlowPopup()
{
    if (!mCurrentScene) {
        return;
    }

    if (mGroupFlowPopup) {
        mGroupFlowPopup->deleteLater();
        mGroupFlowPopup = nullptr;
    }

    auto *popup = new QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint);
    popup->setObjectName(QStringLiteral("AeTimelineFlowPopup"));
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setStyleSheet(QStringLiteral(
        "QFrame#AeTimelineFlowPopup { background: rgb(38,38,42); border: 1px solid rgba(255,255,255,30); border-radius: 8px; }"
        "QPushButton#AeTimelineFlowNode { color: rgb(232,232,232); background: rgba(255,255,255,10); border: 1px solid rgba(255,255,255,22); border-radius: 6px; padding: 5px 9px; }"
        "QPushButton#AeTimelineFlowNode:hover { background: rgba(114,164,255,55); }"
        "QPushButton#AeTimelineFlowNode[current=\"true\"] { background: rgba(114,164,255,88); border-color: rgba(114,164,255,120); }"
        "QLabel#AeTimelineFlowArrow { color: rgba(255,255,255,110); padding: 0 4px; }"));

    auto *layout = new QHBoxLayout(popup);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);

    const auto sceneChain = MainWindow::sGetInstance()
                                ? MainWindow::sGetInstance()->sceneNavigationChain()
                                : QList<Canvas*>();

    auto addArrow = [popup, layout]() {
        auto *arrow = new QLabel(QStringLiteral(">"), popup);
        arrow->setObjectName(QStringLiteral("AeTimelineFlowArrow"));
        layout->addWidget(arrow);
    };

    auto addNode = [popup, layout](const QString &name,
                                   const bool current,
                                   const std::function<void()> &fn) {
        auto *button = new QPushButton(name, popup);
        button->setObjectName(QStringLiteral("AeTimelineFlowNode"));
        button->setProperty("current", current);
        QObject::connect(button, &QPushButton::clicked, popup, [popup, fn]() {
            fn();
            popup->close();
        });
        layout->addWidget(button);
    };

    // Build group chain for the CURRENT scene (groups within composition)
    QList<ContainerBox*> groupChain;
    auto *cursor = mCurrentScene->getCurrentGroup();
    while (cursor && cursor != mCurrentScene) {
        groupChain.prepend(cursor);
        cursor = cursor->getParentGroup();
    }

    const bool hasGroups = !groupChain.isEmpty();

    // Always show scene chain breadcrumbs (multi-comp navigation)
    if (sceneChain.count() > 1) {
        for (int i = 0; i < sceneChain.count(); ++i) {
            if (i > 0) {
                addArrow();
            }

            auto *scene = sceneChain.at(i);
            const bool isCurrentScene = (scene == mCurrentScene);
            addNode(scene->prp_getName(),
                    isCurrentScene && !hasGroups,
                    [scene]() {
                activateSceneWorkspace(*Document::sInstance, scene);
            });

            // If this is the current scene and we're inside a group,
            // show the group path after the scene name
            if (isCurrentScene && hasGroups) {
                for (int g = 0; g < groupChain.count(); ++g) {
                    auto *group = groupChain.at(g);
                    addArrow();
                    addNode(group->prp_getName(),
                            g == groupChain.count() - 1,
                            [group, scene]() {
                        activateSceneWorkspace(*Document::sInstance, scene);
                        scene->setCurrentBoxesGroup(group);
                    });
                }
            }
        }
    } else {
        // Single composition — just the scene name + group path
        addNode(mCurrentScene->prp_getName(), !hasGroups, [this]() {
            mCurrentScene->setCurrentBoxesGroup(mCurrentScene);
            clearAeRevealPreset();
            setTarget(SWT_Target::canvas);
        });

        for (int i = 0; i < groupChain.count(); ++i) {
            addArrow();
            auto *group = groupChain.at(i);
            addNode(group->prp_getName(),
                    i == groupChain.count() - 1,
                    [this, group]() { enterGroup(group); });
        }
    }

    const QPoint globalPos = QCursor::pos() + QPoint(0, 18);
    popup->adjustSize();
    popup->move(globalPos.x() - popup->width()/2, globalPos.y());
    popup->show();
    mGroupFlowPopup = popup;
    connect(popup, &QObject::destroyed, this, [this]() { mGroupFlowPopup = nullptr; });
}

void TimelineWidget::setCurrentScene(Canvas * const scene) {
    if(scene == mCurrentScene) return;
    if(mCurrentScene) {
        disconnect(mCurrentScene, nullptr, mFrameScrollBar, nullptr);
        disconnect(mCurrentScene, nullptr, this, nullptr);
    }

    mCurrentScene = scene;
    const QSignalBlocker chooserBlocker(mSceneChooser);
    mSceneChooser->setCurrentScene(scene);
    mFrameScrollBar->setCurrentCanvas(scene);
    mFrameRangeScrollBar->setCurrentCanvas(scene);
    mBoxesListWidget->setCurrentScene(scene);
    mKeysView->setCurrentScene(scene);
    if(scene) {
        const auto range = scene->getFrameRange();
        setCanvasFrameRange(range);
        mFrameScrollBar->setFirstViewedFrame(scene->getCurrentFrame());
        mFrameRangeScrollBar->setFirstViewedFrame(scene->getCurrentFrame());
        setViewedFrameRange(range);

        connect(scene, &Canvas::currentFrameChanged,
                mFrameScrollBar, &FrameScrollBar::setFirstViewedFrame);
        connect(scene, &Canvas::currentFrameChanged,
                this, &TimelineWidget::ensureCurrentFrameVisible);
        connect(scene, &Canvas::newFrameRange,
                this, &TimelineWidget::setCanvasFrameRange);


        const auto rules = mBoxesListWidget->getRulesCollection();
        if(rules.fTarget == SWT_Target::canvas) {
            mBoxesListWidget->scheduleContentUpdateIfIsCurrentTarget(
                        scene, SWT_Target::canvas);
        } else if(rules.fTarget == SWT_Target::group) {
            mBoxesListWidget->scheduleContentUpdateIfIsCurrentTarget(
                        scene->getCurrentGroup(), SWT_Target::group);
        }
        connect(scene, &Canvas::currentContainerSet, this,
                [this](ContainerBox* const container) {
            mBoxesListWidget->scheduleContentUpdateIfIsCurrentTarget(
                        container, SWT_Target::group);
        });
        connect(scene, &Canvas::requestUpdate, this, [this, scene]() {
            refreshTimelineFromSceneUpdate(scene->isPreviewingOrRendering());
        });
    }
}

void TimelineWidget::setGraphEnabled(const bool enabled) {
    mKeysView->setGraphViewed(enabled);
    mAnimationDockWidget->showGraph(enabled);
    //const auto iconsDir = eSettings::sIconsDir();
    //if(enabled) mGraphAct->setIcon(QIcon(iconsDir + "/graphEnabled.png"));
    //else mGraphAct->setIcon(QIcon(iconsDir + "/graphDisabled.png"));
}

void TimelineWidget::writeState(eWriteStream &dst) const {
    const int id = mBoxesListWidget->getId();
    dst.objListIdConv().assign(id);

    if(mCurrentScene) {
        dst << mCurrentScene->getWriteId();
        dst << mCurrentScene->getDocumentId();
    } else {
        dst << -1;
        dst << -1;
    }

    dst << mSearchLine->text();
    dst << mBoxesListScrollArea->verticalScrollBar()->sliderPosition();

    dst << mFrameScrollBar->getFirstViewedFrame();
    dst << mFrameRangeScrollBar->getFirstViewedFrame();
    dst << mFrameRangeScrollBar->getLastViewedFrame();

    const auto rules = mBoxesListWidget->getRulesCollection();
    dst.write(&rules.fRule, sizeof(SWT_BoxRule));
    dst.write(&rules.fType, sizeof(SWT_Type));
    dst.write(&rules.fTarget, sizeof(SWT_Target));
}

void TimelineWidget::readState(eReadStream &src) {
    const int id = mBoxesListWidget->getId();
    src.objListIdConv().assign(id);

    int sceneReadId; src >> sceneReadId;
    int sceneDocumentId; src >> sceneDocumentId;

    QString search; src >> search;
    int sliderPos; src >> sliderPos;

    int frame; src >> frame;
    int minViewedFrame; src >> minViewedFrame;
    int maxViewedFrame; src >> maxViewedFrame;

    if(src.evFileVersion() > 6) {
        SWT_BoxRule boxRule;
        SWT_Type type;
        SWT_Target target;
        src.read(&boxRule, sizeof(SWT_BoxRule));
        src.read(&type, sizeof(SWT_Type));
        src.read(&target, sizeof(SWT_Target));
        setBoxRule(boxRule);
        setType(type);
        setTarget(target);
    }

    src.addReadStreamDoneTask([this, sceneReadId, sceneDocumentId]
                              (eReadStream& src) {
        BoundingBox* sceneBox = nullptr;
        if(sceneReadId != -1)
            sceneBox = src.getBoxByReadId(sceneReadId);
        if(!sceneBox && sceneDocumentId != -1)
            sceneBox = BoundingBox::sGetBoxByDocumentId(sceneDocumentId);

        setCurrentScene(enve_cast<Canvas*>(sceneBox));
    });

    mSearchLine->setText(search);

    //mBoxesListScrollArea->verticalScrollBar()->setSliderPosition(sliderPos);
    //mKeysView->setViewedVerticalRange(sliderPos, sliderPos + mBoxesListScrollArea->height());

    mFrameScrollBar->setFirstViewedFrame(frame);
    setViewedFrameRange({minViewedFrame, maxViewedFrame});
}

void TimelineWidget::readStateXEV(XevReadBoxesHandler& boxReadHandler,
                                  const QDomElement& ele,
                                  RuntimeIdToWriteId& objListIdConv) {
    objListIdConv.assign(mBoxesListWidget->getId());

    const auto frameRangeStr = ele.attribute("frameRange");
    const auto frameStr = ele.attribute("frame");

    const auto frameRangeValStrs = frameRangeStr.splitRef(' ');
    if(frameRangeValStrs.count() != 2)
        RuntimeThrow("Invalid frame range value " + frameRangeStr);

    const auto minViewedFrameStr = frameRangeValStrs.first();
    const int minViewedFrame = XmlExportHelpers::stringToInt(minViewedFrameStr);

    const auto maxViewedFrameStr = frameRangeValStrs.last();
    const int maxViewedFrame = XmlExportHelpers::stringToInt(maxViewedFrameStr);

    const int frame = XmlExportHelpers::stringToInt(frameStr);

    const auto search = ele.attribute("search");

    const auto sceneIdStr = ele.attribute("sceneId");
    const int sceneId = XmlExportHelpers::stringToInt(sceneIdStr);

    boxReadHandler.addXevImporterDoneTask(
                [this, sceneId](const XevReadBoxesHandler& imp) {
        const auto sceneBox = imp.getBoxByReadId(sceneId);
        const auto scene = enve_cast<Canvas*>(sceneBox);
        setCurrentScene(scene);
    });

    const auto boxRuleStr = ele.attribute("objRule");
    const auto boxRule = XmlExportHelpers::stringToEnum<SWT_BoxRule>(boxRuleStr);
    setBoxRule(boxRule);

    const auto typeStr = ele.attribute("objType");
    const auto type = XmlExportHelpers::stringToEnum<SWT_Type>(typeStr);
    setType(type);

    const auto targetStr = ele.attribute("objTarget");
    const auto target = XmlExportHelpers::stringToEnum<SWT_Target>(targetStr);
    setTarget(target);

    mSearchLine->setText(search);

    mFrameScrollBar->setFirstViewedFrame(frame);
    setViewedFrameRange({minViewedFrame, maxViewedFrame});
}

void TimelineWidget::writeStateXEV(QDomElement& ele, QDomDocument& doc,
                                   RuntimeIdToWriteId& objListIdConv) const {
    Q_UNUSED(doc)

    objListIdConv.assign(mBoxesListWidget->getId());

    const int frame = mFrameScrollBar->getFirstViewedFrame();
    const int minViewedFrame = mFrameRangeScrollBar->getFirstViewedFrame();
    const int maxViewedFrame = mFrameRangeScrollBar->getLastViewedFrame();
    const QString frameRange = QString("%1 %2").arg(minViewedFrame).
                                                arg(maxViewedFrame);

    ele.setAttribute("frameRange", frameRange);
    ele.setAttribute("frame", frame);

    const int sceneId = mCurrentScene ? mCurrentScene->getWriteId() : -1;
    ele.setAttribute("sceneId", sceneId);

    const auto rules = mBoxesListWidget->getRulesCollection();
    ele.setAttribute("objRule", static_cast<int>(rules.fRule));
    ele.setAttribute("objType", static_cast<int>(rules.fType));
    ele.setAttribute("objTarget", static_cast<int>(rules.fTarget));

    ele.setAttribute("search", mSearchLine->text());
}

void TimelineWidget::readSettings(ChangeWidthWidget *chww)
{
    const auto tWidth = AppSupport::getSettings("ui",
                                                "TimeLineMenuWidth");
    setBoxesListWidth(tWidth.isValid() ? tWidth.toInt() : chww->getCurrentWidth());
    if (tWidth.isValid()) { chww->setWidth(tWidth.toInt()); }
}

void TimelineWidget::writeSettings()
{
    AppSupport::setSettings("ui", "TimeLineMenuWidth",
                            mBoxesListScrollArea->width());
}

void TimelineWidget::moveSlider(int val) {
    int diff = val%eSizesUI::widget;
    if(diff != 0) {
        val -= diff;
        mBoxesListScrollArea->verticalScrollBar()->setSliderPosition(val);
    }
    mKeysView->setViewedVerticalRange(val, val + mBoxesListScrollArea->height());
}

void TimelineWidget::setBoxesListWidth(const int width) {
    mMenuWidget->setFixedWidth(width);
    mBoxesListScrollArea->setFixedWidth(width);
}

void TimelineWidget::setBoxRule(const SWT_BoxRule rule) {
    mBoxesListWidget->setCurrentRule(rule);
    emit boxRuleChanged(rule);
}

void TimelineWidget::setTarget(const SWT_Target target) {
    switch(target) {
    case SWT_Target::all:
        mBoxesListWidget->setCurrentTarget(&mDocument, SWT_Target::all);
        break;
    case SWT_Target::canvas:
        mBoxesListWidget->setCurrentTarget(mCurrentScene, SWT_Target::canvas);
        break;
    case SWT_Target::group:
        const auto group = mCurrentScene ? mCurrentScene->getCurrentGroup() :
                                           nullptr;
        mBoxesListWidget->setCurrentTarget(group, SWT_Target::group);
        break;
    }

    emit targetChanged(target);
}

void TimelineWidget::setType(const SWT_Type type) {
    mBoxesListWidget->setCurrentType(type);
    emit typeChanged(type);
}

void TimelineWidget::setSearchText(const QString &text) {
    mBoxesListWidget->setCurrentSearchText(text);
}

void TimelineWidget::setViewedFrameRange(const FrameRange& range) {
    FrameRange clampedRange = range;
    const FrameRange canvasRange = {mFrameRangeScrollBar->getMinFrame(),
                                    mFrameRangeScrollBar->getMaxFrame()};
    const int canvasSpan = qMax(0, canvasRange.fMax - canvasRange.fMin);
    const int requestedSpan = qMax(0, range.fMax - range.fMin);
    const int targetSpan = qMin(requestedSpan, canvasSpan);

    clampedRange.fMin = qMax(range.fMin, canvasRange.fMin);
    clampedRange.fMax = clampedRange.fMin + targetSpan;
    if (clampedRange.fMax > canvasRange.fMax) {
        clampedRange.fMax = canvasRange.fMax;
        clampedRange.fMin = qMax(canvasRange.fMin, clampedRange.fMax - targetSpan);
    }
    if (clampedRange.fMin > clampedRange.fMax) {
        clampedRange = canvasRange;
    }

    mFrameRangeScrollBar->setViewedFrameRange(clampedRange);
    mFrameScrollBar->setDisplayedFrameRange(clampedRange);
    mKeysView->setFramesRange(clampedRange);
}

void TimelineWidget::setCanvasFrameRange(const FrameRange& range) {
    const FrameRange clampedRange = range;
    mFrameRangeScrollBar->setDisplayedFrameRange(clampedRange);
    mFrameRangeScrollBar->setCanvasFrameRange(clampedRange);
    mFrameScrollBar->setCanvasFrameRange(clampedRange);
    setViewedFrameRange(mFrameRangeScrollBar->getViewedRange());
}

void TimelineWidget::ensureCurrentFrameVisible(const int frame)
{
    const FrameRange viewed = mFrameRangeScrollBar->getViewedRange();
    const int span = qMax(1, viewed.fMax - viewed.fMin);
    const int margin = qMax(3, span/24);
    if (frame >= viewed.fMin + margin && frame <= viewed.fMax - margin) {
        mKeysView->update();
        return;
    }

    const int preferredLead = qMax(2, span/3);
    int newMin = frame - preferredLead;
    int newMax = newMin + span;
    const FrameRange canvasRange = {mFrameRangeScrollBar->getMinFrame(),
                                    mFrameRangeScrollBar->getMaxFrame()};
    if (newMin < canvasRange.fMin) {
        newMin = canvasRange.fMin;
        newMax = newMin + span;
    }
    if (newMax > canvasRange.fMax) {
        newMax = canvasRange.fMax;
        newMin = newMax - span;
    }
    setViewedFrameRange({newMin, newMax});
}
