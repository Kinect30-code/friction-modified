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

#include "timelinedockwidget.h"

#include <QKeyEvent>
#include <QLabel>
#include <QDebug>
#include <QScrollBar>
#include <QStatusBar>

#include "Private/document.h"
#include "GUI/global.h"
#include "GUI/BoxesList/boxscrollwidget.h"
#include "GUI/BoxesList/boxsinglewidget.h"

#include "mainwindow.h"
#include "canvaswindow.h"
#include "canvas.h"
#include "Boxes/animationbox.h"
#include "animationdockwidget.h"
#include "widgets/widgetstack.h"
#include "widgets/actionbutton.h"
#include "timelinewidget.h"
#include "widgets/framescrollbar.h"
#include "renderinstancesettings.h"
#include "layouthandler.h"
#include "memoryhandler.h"
#include "appsupport.h"
#include "Sound/soundcomposition.h"

TimelineDockWidget::TimelineDockWidget(Document& document,
                                       LayoutHandler * const layoutH,
                                       MainWindow * const parent)
    : QWidget(parent)
    , mDocument(document)
    , mMainWindow(parent)
    , mTimelineLayout(layoutH->timelineLayout())
    , mToolBar(nullptr)
    , mFrameStartSpin(nullptr)
    , mFrameEndSpin(nullptr)
    , mFrameRewindAct(nullptr)
    , mFrameFastForwardAct(nullptr)
    , mCurrentFrameSpinAct(nullptr)
    , mCurrentFrameSpin(nullptr)
    , mRenderProgressAct(nullptr)
    , mRenderProgress(nullptr)
    , mStepPreviewTimer(nullptr)
    , mIdleCacheTimer(nullptr)
    , mPausedPreviewState({false, 0})
{
    setObjectName("AeTimelineDock");
    connect(RenderHandler::sInstance, &RenderHandler::previewFinished,
            this, &TimelineDockWidget::previewFinished);
    connect(RenderHandler::sInstance, &RenderHandler::previewBeingPlayed,
            this, &TimelineDockWidget::previewBeingPlayed);
    connect(RenderHandler::sInstance, &RenderHandler::previewBeingRendered,
            this, &TimelineDockWidget::previewBeingRendered);
    connect(RenderHandler::sInstance, &RenderHandler::previewPaused,
            this, &TimelineDockWidget::previewPaused);

    connect(Document::sInstance, &Document::canvasModeSet,
            this, &TimelineDockWidget::updateButtonsVisibility);
    connect(Document::sInstance, &Document::canvasModeSet,
            this, [this](CanvasMode) {
        const auto state = RenderHandler::sInstance->currentPreviewState();
        if (state == PreviewState::playing ||
            state == PreviewState::rendering) {
            interruptPreview();
        }
    });

    setFocusPolicy(Qt::NoFocus);

    mMainLayout = new QVBoxLayout(this);
    setLayout(mMainLayout);
    mMainLayout->setSpacing(0);
    mMainLayout->setMargin(0);

    mFrameRewindAct = new QAction(QIcon::fromTheme("rewind"),
                                  tr("Rewind"),
                                  this);
    mFrameRewindAct->setShortcut(QKeySequence(AppSupport::getSettings("shortcuts",
                                                                      "rewind",
                                                                      "Shift+Left").toString()));
    mFrameRewindAct->setData(tr("Go to First Frame"));
    connect(mFrameRewindAct, &QAction::triggered,
            this, [this]() {
        const auto scene = *mDocument.fActiveScene;
        if (!scene) { return; }
        const bool jumpFrame = (QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::AltModifier)) == (Qt::ShiftModifier | Qt::AltModifier);
        if (jumpFrame) { // Go to previous scene quarter
            jumpToIntermediateFrame(false);
        } else { // Go to First Frame
            scene->anim_setAbsFrame(scene->getFrameRange().fMin);
            mDocument.actionFinished();
        }
    });

    mFrameFastForwardAct = new QAction(QIcon::fromTheme("fastforward"),
                                       tr("Fast Forward"),
                                       this);
    mFrameFastForwardAct->setShortcut(QKeySequence(AppSupport::getSettings("shortcuts",
                                                                           "fastForward",
                                                                           "Shift+Right").toString()));
    mFrameFastForwardAct->setData(tr("Go to Last Frame"));
    connect(mFrameFastForwardAct, &QAction::triggered,
            this, [this]() {
        const auto scene = *mDocument.fActiveScene;
        if (!scene) { return; }
        const bool jumpFrame = (QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::AltModifier)) == (Qt::ShiftModifier | Qt::AltModifier);
        if (jumpFrame) { // Go to next scene quarter
            jumpToIntermediateFrame(true);
        } else { // Go to Last Frame
            scene->anim_setAbsFrame(scene->getFrameRange().fMax);
            mDocument.actionFinished();
        }
    });

    mPlayFromBeginningButton = new QAction(QIcon::fromTheme("preview"),
                                           tr("Play Preview From Start"),
                                           this);
    connect(mPlayFromBeginningButton, &QAction::triggered,
            this, [this]() {
        /*const auto scene = *mDocument.fActiveScene;
        if (!scene) { return; }
        scene->anim_setAbsFrame(scene->getFrameRange().fMin);
        renderPreview();*/
        const auto state = RenderHandler::sInstance->currentPreviewState();
        setPreviewFromStart(state);
    });

    mPlayButton = new QAction(QIcon::fromTheme("play"),
                              tr("Play Preview"),
                              this);

    mStopButton = new QAction(QIcon::fromTheme("stop"),
                              tr("Stop Preview"),
                              this);

    connect(mStopButton, &QAction::triggered,
            this, &TimelineDockWidget::interruptPreview);

    mLoopButton = new QAction(QIcon::fromTheme("preview_loop"),
                              tr("Loop Preview"),
                              this);
    mLoopButton->setCheckable(true);
    connect(mLoopButton, &QAction::triggered,
            this, &TimelineDockWidget::setLoop);

    mStepPreviewTimer = new QTimer(this);
    mIdleCacheTimer = new QTimer(this);
    mIdleCacheTimer->setSingleShot(true);
    mIdleCacheTimer->setInterval(150);

    mFrameStartSpin = new FrameSpinBox(this);
    mFrameStartSpin->setKeyboardTracking(false);
    mFrameStartSpin->setObjectName("LeftSpinBox");
    mFrameStartSpin->setAlignment(Qt::AlignHCenter);
    mFrameStartSpin->setFocusPolicy(Qt::ClickFocus);
    mFrameStartSpin->setToolTip(tr("Work area start"));
    mFrameStartSpin->setRange(0, INT_MAX);
    connect(mFrameStartSpin,
            &QSpinBox::editingFinished,
            this, [this]() {
            const auto scene = *mDocument.fActiveScene;
            if (!scene) { return; }
            auto range = scene->getFrameRange();
            int frame = mFrameStartSpin->value();
            if (range.fMin == frame) { return; }
            if (frame >= range.fMax) {
                mFrameStartSpin->setValue(range.fMin);
                return;
            }
            range.fMin = frame;
            scene->setFrameRange(range);
    });

    mFrameEndSpin = new FrameSpinBox(this);
    mFrameEndSpin->setKeyboardTracking(false);
    mFrameEndSpin->setAlignment(Qt::AlignHCenter);
    mFrameEndSpin->setFocusPolicy(Qt::ClickFocus);
    mFrameEndSpin->setToolTip(tr("Work area end"));
    mFrameEndSpin->setRange(1, INT_MAX);
    connect(mFrameEndSpin,
            &QSpinBox::editingFinished,
            this, [this]() {
            const auto scene = *mDocument.fActiveScene;
            if (!scene) { return; }
            auto range = scene->getFrameRange();
            int frame = mFrameEndSpin->value();
            if (range.fMax == frame) { return; }
            if (frame <= range.fMin) {
                mFrameEndSpin->setValue(range.fMax);
                return;
            }
            range.fMax = frame;
            scene->setFrameRange(range);
    });

    mCurrentFrameSpin = new FrameSpinBox(this);
    mCurrentFrameSpin->setKeyboardTracking(false);
    mCurrentFrameSpin->setAlignment(Qt::AlignHCenter);
    mCurrentFrameSpin->setObjectName(QString::fromUtf8("SpinBoxNoButtons"));
    mCurrentFrameSpin->setFocusPolicy(Qt::ClickFocus);
    mCurrentFrameSpin->setToolTip(tr("Current frame"));
    mCurrentFrameSpin->setRange(-INT_MAX, INT_MAX);
    connect(mCurrentFrameSpin,
            &QSpinBox::editingFinished,
            this, [this]() { gotoFrame(mCurrentFrameSpin->value()); });
    connect(mCurrentFrameSpin,
            &FrameSpinBox::wheelValueChanged,
            this, &TimelineDockWidget::gotoFrame);

    const auto mPrevKeyframeAct = new QAction(QIcon::fromTheme("prev_keyframe"),
                                              QString(),
                                              this);
    mPrevKeyframeAct->setToolTip(tr("Previous Keyframe"));
    mPrevKeyframeAct->setData(mPrevKeyframeAct->toolTip());
    connect(mPrevKeyframeAct, &QAction::triggered,
            this, [this]() {
        if (setPrevKeyframe()) {
            mDocument.actionFinished();
        }
    });

    const auto mNextKeyframeAct = new QAction(QIcon::fromTheme("next_keyframe"),
                                              QString(),
                                              this);
    mNextKeyframeAct->setToolTip(tr("Next Keyframe"));
    mNextKeyframeAct->setData(mNextKeyframeAct->toolTip());
    connect(mNextKeyframeAct, &QAction::triggered,
            this, [this]() {
        if (setNextKeyframe()) {
            mDocument.actionFinished();
        }
    });

    mToolBar = new QToolBar(this);
    mToolBar->setObjectName("AeTimelineTransport");
    mToolBar->setMovable(false);

    mRenderProgress = new QProgressBar(this);
    mRenderProgress->setObjectName("RenderProgressBar");
    mRenderProgress->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Expanding);
    mRenderProgress->setFixedWidth(mCurrentFrameSpin->width());
    mRenderProgress->setFormat(tr("Cache %p%"));

    eSizesUI::widget.add(mToolBar, [this](const int size) {
        //mRenderProgress->setFixedHeight(eSizesUI::button);
        mToolBar->setIconSize(QSize(size, size));
    });

    const auto addToolbarLabel = [this](const QString &text,
                                        const QString &tooltip = QString()) {
        const auto label = new QLabel(text, this);
        label->setObjectName("AeTimelineToolbarLabel");
        label->setAlignment(Qt::AlignCenter);
        if (!tooltip.isEmpty()) {
            label->setToolTip(tooltip);
        }
        mToolBar->addWidget(label);
    };

    // start layout
    addToolbarLabel(tr("IN"), tr("Work area start"));
    mToolBar->addWidget(mFrameStartSpin);

    addSpacer();

    mToolBar->addAction(mFrameRewindAct);
    mToolBar->addAction(mPrevKeyframeAct);
    mToolBar->addAction(mNextKeyframeAct);
    mToolBar->addAction(mFrameFastForwardAct);

    mRenderProgressAct = mToolBar->addWidget(mRenderProgress);
    addToolbarLabel(tr("TIME"), tr("Current time"));
    mCurrentFrameSpinAct = mToolBar->addWidget(mCurrentFrameSpin);

    mToolBar->addAction(mPlayFromBeginningButton);
    mToolBar->addAction(mPlayButton);
    mToolBar->addAction(mLoopButton);

    addSpacer();

    addToolbarLabel(tr("OUT"), tr("Work area end"));
    mToolBar->addWidget(mFrameEndSpin);
    // end layout

    mRenderProgressAct->setVisible(false);

    mMainWindow->cmdAddAction(mFrameRewindAct);
    mMainWindow->cmdAddAction(mPrevKeyframeAct);
    mMainWindow->cmdAddAction(mNextKeyframeAct);
    mMainWindow->cmdAddAction(mFrameFastForwardAct);
    mMainWindow->cmdAddAction(mPlayFromBeginningButton);
    mMainWindow->cmdAddAction(mPlayButton);
    mMainWindow->cmdAddAction(mStopButton);
    mMainWindow->cmdAddAction(mLoopButton);

    mMainLayout->addWidget(mToolBar);
    mMainLayout->addSpacing(2);

    mPlayFromBeginningButton->setEnabled(false);
    mPlayButton->setEnabled(false);
    mStopButton->setEnabled(false);

    connect(&mDocument, &Document::activeSceneSet,
            this, [this](Canvas* const scene) {
        mPlayFromBeginningButton->setEnabled(scene);
        mPlayButton->setEnabled(scene);
        mStopButton->setEnabled(scene);
    });

    mMainLayout->addWidget(mTimelineLayout);

    previewFinished();

    connect(&mDocument, &Document::activeSceneSet,
            this, &TimelineDockWidget::updateSettingsForCurrentCanvas);

    connect(mStepPreviewTimer, &QTimer::timeout,
            this, &TimelineDockWidget::stepPreview);
    connect(mIdleCacheTimer, &QTimer::timeout,
            this, &TimelineDockWidget::processIdlePreviewCache);
    connect(&mDocument, &Document::documentChanged,
            this, &TimelineDockWidget::scheduleIdlePreviewCache);

}

void TimelineDockWidget::updateFrameRange(const FrameRange &range)
{
    mRenderProgress->setRange(range.fMin, range.fMax);
    if (range.fMin != mFrameStartSpin->value()) {
        mFrameStartSpin->blockSignals(true);
        mFrameStartSpin->setValue(range.fMin);
        mFrameStartSpin->blockSignals(false);
    }
    if (range.fMax != mFrameEndSpin->value()) {
        mFrameEndSpin->blockSignals(true);
        mFrameEndSpin->setValue(range.fMax);
        mFrameEndSpin->blockSignals(false);
    }
}

void TimelineDockWidget::handleCurrentFrameChanged(int frame)
{
    mCurrentFrameSpin->setValue(frame);
    if (mRenderProgress->isVisible()) { mRenderProgress->setValue(frame); }
    if (eSettings::instance().fPreviewCache &&
        RenderHandler::sInstance->currentPreviewState() == PreviewState::paused) {
        RenderHandler::sInstance->setPreviewFrame(frame);
    }
    scheduleIdlePreviewCache();
}

void TimelineDockWidget::scheduleIdlePreviewCache()
{
    if (!eSettings::instance().fPreviewCache || !mIdleCacheTimer) { return; }
    const auto state = RenderHandler::sInstance->currentPreviewState();
    if (state == PreviewState::playing || state == PreviewState::rendering) {
        mIdleCacheTimer->stop();
        return;
    }
    if (!mDocument.fActiveScene) { return; }
    mIdleCacheTimer->start();
}

void TimelineDockWidget::processIdlePreviewCache()
{
    if (!eSettings::instance().fPreviewCache) { return; }
    const auto state = RenderHandler::sInstance->currentPreviewState();
    if (state == PreviewState::playing || state == PreviewState::rendering) {
        return;
    }

    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }

    RenderHandler::sInstance->cacheAroundFrame(scene->anim_getCurrentAbsFrame());
}

void TimelineDockWidget::showRenderStatus(bool show)
{
    if (!show) { mRenderProgress->setValue(0); }
    mCurrentFrameSpinAct->setVisible(!show);
    mRenderProgressAct->setVisible(show);
}

void TimelineDockWidget::addSpacer()
{
    const auto spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding,
                          QSizePolicy::Minimum);
    mToolBar->addWidget(spacer);
}

void TimelineDockWidget::addBlankAction()
{
    const auto act = mToolBar->addAction(QString());
    act->setEnabled(false);
}

void TimelineDockWidget::setLoop(const bool loop)
{
    RenderHandler::sInstance->setLoop(loop);
}

bool TimelineDockWidget::processKeyPress(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;
    const auto state = RenderHandler::sInstance->currentPreviewState();
    const bool jumpFrame = (mods & (Qt::ShiftModifier | Qt::AltModifier)) == (Qt::ShiftModifier | Qt::AltModifier);
    if (ctrl && !alt && !(mods & Qt::MetaModifier) && key == Qt::Key_E) {
        const auto widget = currentTimelineWidget();
        if (!widget) { return false; }
        widget->showSelectedKeyEaseMenu();
        return true;
    }
    if (!ctrl && !alt && !(mods & Qt::MetaModifier)) {
        switch (key) {
        case Qt::Key_T: {
            if (!shift) { break; }
            const auto widget = currentTimelineWidget();
            if (!widget) { return false; }
            widget->showSelectedKeyStrengthMenu();
            return true;
        }
        case Qt::Key_U: {
            if (!shift) { break; }
            const auto widget = currentTimelineWidget();
            if (!widget) { return false; }
            widget->toggleSelectedTransformVisibility();
            return true;
        }
        default:
            break;
        }
    }
    if (key == Qt::Key_Escape) { // stop playback
        if (state != PreviewState::stopped ||
            mStepPreviewTimer->isActive()) { interruptPreview(); }
        else { return false; }

    } else if (key == Qt::Key_Space && shift) { // play from first frame
        if (!setPreviewFromStart(state)) { return false; }
    } else if (key == Qt::Key_Space) { // start/resume playback
        if (!eSettings::instance().fPreviewCache) {
            if (mStepPreviewTimer->isActive()) { stopPreview(); }
            else { playPreview(); }
        } else {
            switch (state) {
                case PreviewState::stopped: renderPreview(); break;
                case PreviewState::rendering: playPreview(); break;
                case PreviewState::playing: stopPreview(); break;
                case PreviewState::paused: resumePreview(); break;
            }
        }
    } else if (ctrl && shift && key == Qt::Key_D) { // split clip
        splitClip();
    } else if (key == Qt::Key_B && !ctrl && !alt) { // work area in
        setIn();
    } else if (key == Qt::Key_N && !ctrl && !alt) { // work area out
        setOut();
    } else if (key == Qt::Key_BracketLeft && !ctrl) {
        if (alt) {
            if (!trimSelectedLayersBeforeCurrentFrame()) { return false; }
        } else {
            if (!moveSelectedLayerInToCurrentFrame()) { return false; }
        }
    } else if (key == Qt::Key_BracketLeft && ctrl && !alt) {
        if (mDocument.fActiveScene) {
            mDocument.fActiveScene->lowerSelectedBoxes();
        } else {
            return false;
        }
    } else if (key == Qt::Key_BracketRight && !ctrl) {
        if (alt) {
            if (!trimSelectedLayersAfterCurrentFrame()) { return false; }
        } else {
            if (!moveSelectedLayerOutToCurrentFrame()) { return false; }
        }
    } else if (key == Qt::Key_BracketRight && ctrl && !alt) {
        if (mDocument.fActiveScene) {
            mDocument.fActiveScene->raiseSelectedBoxes();
        } else {
            return false;
        }
    } else if (key == Qt::Key_Asterisk) { // set marker
        setMarker();
    } else if (key == Qt::Key_I || key == Qt::Key_O) { // legacy frame in/out
        switch(key) {
            case Qt::Key_I: setIn(); break;
            case Qt::Key_O: setOut(); break;
            default:;
        }
    } else if (key == Qt::Key_PageUp && !ctrl && !alt) {
        if (!shiftCurrentFrameBy(shift ? -10 : -1)) { return false; }
    } else if (key == Qt::Key_PageDown && !ctrl && !alt) {
        if (!shiftCurrentFrameBy(shift ? 10 : 1)) { return false; }
    } else if (key == Qt::Key_J && !ctrl && !alt) {
        if (!setPrevKeyframe()) { return false; }
    } else if (key == Qt::Key_K && !ctrl && !alt) {
        if (!setNextKeyframe()) { return false; }
    } else if (key == Qt::Key_Right && !(mods & Qt::ControlModifier)) {
        if (jumpFrame) { // jump to next scene quarter
            jumpToIntermediateFrame(true);
        } else { // next frame
            mDocument.incActiveSceneFrame();
        }
    } else if (key == Qt::Key_Left && !(mods & Qt::ControlModifier)) {
        if (jumpFrame) { // jump to previous scene quarter
            jumpToIntermediateFrame(false);
        } else { // previous frame
            mDocument.decActiveSceneFrame();
        }
    } else if (key == Qt::Key_Down && !(mods & Qt::ControlModifier)) { // previous keyframe
        /*const auto scene = *mDocument.fActiveScene;
        if (!scene) { return false; }
        int targetFrame;
        const int frame = mDocument.getActiveSceneFrame();
        if (scene->anim_prevRelFrameWithKey(frame, targetFrame)) {
            mDocument.setActiveSceneFrame(targetFrame);
        }*/
        if (!setPrevKeyframe()) { return false; }
    } else if (key == Qt::Key_Up && !(mods & Qt::ControlModifier)) { // next keyframe
        /*const auto scene = *mDocument.fActiveScene;
        if (!scene) { return false; }
        int targetFrame;
        const int frame = mDocument.getActiveSceneFrame();
        if (scene->anim_nextRelFrameWithKey(frame, targetFrame)) {
            mDocument.setActiveSceneFrame(targetFrame);
        }*/
        if (!setNextKeyframe()) { return false; }
    } else {
        return false;
    }
    return true;
}

void TimelineDockWidget::previewFinished()
{
    if (mIdleCacheTimer) { mIdleCacheTimer->stop(); }
    mPausedPreviewState.first = false;
    if (const auto scene = *mDocument.fActiveScene) {
        scene->setGizmosSuppressed(false);
    }
    //setPlaying(false);
    mFrameStartSpin->setEnabled(true);
    mFrameEndSpin->setEnabled(true);
    mCurrentFrameSpinAct->setEnabled(true);
    showRenderStatus(false);
    mPlayFromBeginningButton->setDisabled(false);
    mStopButton->setDisabled(true);
    mPlayButton->setIcon(QIcon::fromTheme("play"));
    mPlayButton->setText(tr("Play Preview"));
    disconnect(mPlayButton, nullptr, this, nullptr);
    connect(mPlayButton, &QAction::triggered,
            this, &TimelineDockWidget::renderPreview);
    scheduleIdlePreviewCache();
}

void TimelineDockWidget::previewBeingPlayed()
{
    if (mIdleCacheTimer) { mIdleCacheTimer->stop(); }
    if (const auto scene = *mDocument.fActiveScene) {
        scene->setGizmosSuppressed(true);
    }
    mFrameStartSpin->setEnabled(false);
    mFrameEndSpin->setEnabled(false);
    mCurrentFrameSpinAct->setEnabled(false);
    showRenderStatus(false);
    mPlayFromBeginningButton->setDisabled(true);
    mStopButton->setDisabled(false);
    mPlayButton->setIcon(QIcon::fromTheme("stop"));
    mPlayButton->setText(tr("Stop Preview"));
    disconnect(mPlayButton, nullptr, this, nullptr);
    connect(mPlayButton, &QAction::triggered,
            this, &TimelineDockWidget::stopPreview);
}

void TimelineDockWidget::previewBeingRendered()
{
    if (mIdleCacheTimer) { mIdleCacheTimer->stop(); }
    mFrameStartSpin->setEnabled(false);
    mFrameEndSpin->setEnabled(false);
    mCurrentFrameSpinAct->setEnabled(false);
    showRenderStatus(true);
    mPlayFromBeginningButton->setDisabled(true);
    mStopButton->setDisabled(false);
    mPlayButton->setIcon(QIcon::fromTheme("play"));
    mPlayButton->setText(tr("Play Preview"));
    disconnect(mPlayButton, nullptr, this, nullptr);
    connect(mPlayButton, &QAction::triggered,
            this, &TimelineDockWidget::playPreview);
}

void TimelineDockWidget::previewPaused()
{
    if (mIdleCacheTimer) { mIdleCacheTimer->stop(); }
    mPausedPreviewState = {true, mDocument.getActiveSceneFrame()};

    if (const auto scene = *mDocument.fActiveScene) {
        scene->setGizmosSuppressed(false);
    }
    mFrameStartSpin->setEnabled(true);
    mFrameEndSpin->setEnabled(true);
    mCurrentFrameSpinAct->setEnabled(true);
    showRenderStatus(false);
    mPlayFromBeginningButton->setDisabled(true);
    mStopButton->setDisabled(false);
    mPlayButton->setIcon(QIcon::fromTheme("play"));
    mPlayButton->setText(tr("Resume Preview"));
    disconnect(mPlayButton, nullptr, this, nullptr);
    connect(mPlayButton, &QAction::triggered,
            this, &TimelineDockWidget::resumePreview);
    scheduleIdlePreviewCache();
}

bool TimelineDockWidget::setPreviewFromStart(PreviewState state)
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }
    if (state != PreviewState::stopped) { interruptPreview(); }
    scene->anim_setAbsFrame(scene->getFrameRange().fMin);
    renderPreview();
    return true;
}

bool TimelineDockWidget::setNextKeyframe()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }
    int targetFrame;
    const int frame = mDocument.getActiveSceneFrame();
    if (scene->anim_nextRelFrameWithKey(frame, targetFrame)) {
        mDocument.setActiveSceneFrame(targetFrame);
    }
    return true;
}

bool TimelineDockWidget::setPrevKeyframe()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }
    int targetFrame;
    const int frame = mDocument.getActiveSceneFrame();
    if (scene->anim_prevRelFrameWithKey(frame, targetFrame)) {
        mDocument.setActiveSceneFrame(targetFrame);
    }
    return true;
}

void TimelineDockWidget::resumePreview()
{
    if (eSettings::instance().fPreviewCache) {
        if (mPausedPreviewState.first) {
            const int frame = mDocument.getActiveSceneFrame();
            if (mPausedPreviewState.second != frame) {
                qDebug() << "set new start frame for preview" << frame;
                RenderHandler::sInstance->setPreviewFrame(frame);
                mPausedPreviewState.first = false;
            }
        }
        RenderHandler::sInstance->resumePreview();
    } else { setStepPreviewStart(); }
}

void TimelineDockWidget::setStepPreviewStop(const bool pause)
{
    mStepPreviewTimer->stop();
    if (pause) { previewPaused(); }
    else { previewFinished(); }
}

void TimelineDockWidget::setStepPreviewStart()
{
    if (eSettings::instance().fPreviewCache) { return; }

    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }

    if (mStepPreviewTimer->isActive()) {
        mStepPreviewTimer->stop();
    }

    const auto state = RenderHandler::sInstance->currentPreviewState();
    if (state != PreviewState::stopped) {
        RenderHandler::sInstance->interruptPreview();
    }

    int fps = scene->getFps();
    mStepPreviewTimer->setInterval(1000 / fps);
    mStepPreviewTimer->start();
    previewBeingPlayed();
}

void TimelineDockWidget::gotoFrame(int frame)
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    scene->anim_setAbsFrame(frame);
    mDocument.actionFinished();
}

void TimelineDockWidget::updateButtonsVisibility(const CanvasMode mode)
{
    Q_UNUSED(mode)
}

void TimelineDockWidget::pausePreview()
{
    if (eSettings::instance().fPreviewCache) {
        RenderHandler::sInstance->pausePreview();
    } else { setStepPreviewStop(); }
}

void TimelineDockWidget::playPreview()
{
    if (eSettings::instance().fPreviewCache) {
        RenderHandler::sInstance->playPreview();
    } else { setStepPreviewStart(); }
}

void TimelineDockWidget::renderPreview()
{
    if (eSettings::instance().fPreviewCache) {
        RenderHandler::sInstance->playPreview();
    } else { setStepPreviewStart(); }
}

void TimelineDockWidget::interruptPreview()
{
    if (eSettings::instance().fPreviewCache) {
        RenderHandler::sInstance->interruptPreview();
    } else { setStepPreviewStop(); }
}

void TimelineDockWidget::updateSettingsForCurrentCanvas(Canvas* const canvas)
{
    if (auto *widget = currentTimelineWidget()) {
        widget->setCurrentScene(canvas);
    }

    if (!canvas) { return; }

    const auto range = canvas->getFrameRange();
    updateFrameRange(range);
    handleCurrentFrameChanged(canvas->anim_getCurrentAbsFrame());

    mCurrentFrameSpin->setDisplayTimeCode(canvas->getDisplayTimecode());
    mFrameStartSpin->setDisplayTimeCode(canvas->getDisplayTimecode());
    mFrameEndSpin->setDisplayTimeCode(canvas->getDisplayTimecode());

    mCurrentFrameSpin->updateFps(canvas->getFps());
    mFrameStartSpin->updateFps(canvas->getFps());
    mFrameEndSpin->updateFps(canvas->getFps());

    connect(canvas, &Canvas::fpsChanged,
            this, [this](const qreal fps) {
        mCurrentFrameSpin->updateFps(fps);
        mFrameStartSpin->updateFps(fps);
        mFrameEndSpin->updateFps(fps);
        if (mStepPreviewTimer->isActive()) {
            mStepPreviewTimer->setInterval(1000 / fps);
        }
    });
    connect(canvas, &Canvas::displayTimeCodeChanged,
            this, [this](const bool enabled) {
        mCurrentFrameSpin->setDisplayTimeCode(enabled);
        mFrameStartSpin->setDisplayTimeCode(enabled);
        mFrameEndSpin->setDisplayTimeCode(enabled);
    });

    connect(canvas,
            &Canvas::newFrameRange,
            this, [this](const FrameRange range) {
            updateFrameRange(range);
    });
    connect(canvas, &Canvas::currentFrameChanged,
            this, &TimelineDockWidget::handleCurrentFrameChanged);

    update(); // needed for loaded markers
    scheduleIdlePreviewCache();
}

void TimelineDockWidget::stopPreview()
{
    switch (RenderHandler::sInstance->currentPreviewState()) {
    case PreviewState::paused:
    case PreviewState::playing:
    case PreviewState::rendering:
        interruptPreview();
        break;
    default:;
    }
}

void TimelineDockWidget::setIn()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    const auto frame = scene->getCurrentFrame();
    if (scene->getFrameOut().enabled) {
        if (frame >= scene->getFrameOut().frame) { return; }
    }
    bool apply = frame == 0 ? true : (scene->getFrameIn().frame != frame);
    scene->setFrameIn(apply, frame);
}

void TimelineDockWidget::setOut()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    const auto frame = scene->getCurrentFrame();
    if (scene->getFrameIn().enabled) {
        if (frame <= scene->getFrameIn().frame) { return; }
    }
    bool apply = (scene->getFrameOut().frame != frame);
    scene->setFrameOut(apply, frame);
}

void TimelineDockWidget::setMarker()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    const auto frame = scene->getCurrentFrame();
    scene->setMarker(frame);
}

void TimelineDockWidget::splitClip()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    scene->splitAction();
}

bool TimelineDockWidget::moveSelectedLayerInToCurrentFrame()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }
    return scene->moveSelectedLayerInPointToFrame(scene->anim_getCurrentAbsFrame());
}

bool TimelineDockWidget::moveSelectedLayerOutToCurrentFrame()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }
    return scene->moveSelectedLayerOutPointToFrame(scene->anim_getCurrentAbsFrame());
}

bool TimelineDockWidget::trimSelectedLayersBeforeCurrentFrame()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }
    return scene->trimSelectedLayersBeforeFrame(scene->anim_getCurrentAbsFrame());
}

bool TimelineDockWidget::trimSelectedLayersAfterCurrentFrame()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }
    return scene->trimSelectedLayersAfterFrame(scene->anim_getCurrentAbsFrame());
}

bool TimelineDockWidget::shiftCurrentFrameBy(int delta)
{
    if (!mDocument.fActiveScene) { return false; }
    if (delta > 0) {
        for (int i = 0; i < delta; i++) { mDocument.incActiveSceneFrame(); }
    } else if (delta < 0) {
        for (int i = 0; i < -delta; i++) { mDocument.decActiveSceneFrame(); }
    }
    return delta != 0;
}

TimelineWidget *TimelineDockWidget::currentTimelineWidget() const
{
    if (!mTimelineLayout) { return nullptr; }

    if (auto *widget = qobject_cast<TimelineWidget*>(mTimelineLayout->currentWidget())) {
        return widget;
    }

    const auto currentPage = mTimelineLayout->currentWidget();
    if (!currentPage) { return nullptr; }

    return currentPage->findChild<TimelineWidget*>(QStringLiteral("AeTimelineWidget"));
}

bool TimelineDockWidget::applyAeRevealPreset(BoxScrollWidget::AeRevealPreset preset)
{
    const auto widget = currentTimelineWidget();
    if (!widget) { return false; }
    widget->applyAeRevealPreset(preset);
    return true;
}

bool TimelineDockWidget::revealSelectedMasks()
{
    const auto widget = currentTimelineWidget();
    if (!widget) { return false; }
    widget->revealSelectedMasks();
    return true;
}

bool TimelineDockWidget::toggleSelectedTimeRemapping()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }

    const auto selected = scene->selectedBoxesList();
    QList<AnimationBox*> targets;
    bool shouldEnable = false;
    for (auto *box : selected) {
        if (auto *animBox = enve_cast<AnimationBox*>(box)) {
            targets << animBox;
            if (!animBox->frameRemappingEnabled()) {
                shouldEnable = true;
            }
        }
    }

    if (targets.isEmpty()) { return false; }

    for (auto *animBox : targets) {
        if (shouldEnable) {
            animBox->enableFrameRemappingAction();
        } else {
            animBox->disableFrameRemappingAction();
        }
    }

    if (shouldEnable) {
        if (auto *widget = currentTimelineWidget()) {
            widget->revealSelectedFrameRemapping();
        }
        if (mMainWindow && mMainWindow->statusBar()) {
            mMainWindow->statusBar()->showMessage(
                        tr("AE: Time Remapping enabled for %1 layer(s).").arg(targets.count()),
                        3000);
        }
    } else if (mMainWindow && mMainWindow->statusBar()) {
        mMainWindow->statusBar()->showMessage(
                    tr("AE: Time Remapping disabled for %1 layer(s).").arg(targets.count()),
                    3000);
    }

    return true;
}

bool TimelineDockWidget::freezeSelectedFramesAtCurrentTime()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return false; }

    const int absFrame = scene->anim_getCurrentAbsFrame();
    const auto selected = scene->selectedBoxesList();

    QList<AnimationBox*> targets;
    for (auto *box : selected) {
        if (auto *animBox = enve_cast<AnimationBox*>(box)) {
            if (animBox->freezeFrameAtAbsFrameAction(absFrame)) {
                targets << animBox;
            }
        }
    }

    if (targets.isEmpty()) {
        return false;
    }

    if (auto *widget = currentTimelineWidget()) {
        widget->revealSelectedFrameRemapping();
    }

    if (mMainWindow && mMainWindow->statusBar()) {
        mMainWindow->statusBar()->showMessage(
                    tr("AE: Freeze Frame applied to %1 layer(s).")
                    .arg(targets.count()),
                    3000);
    }

    return true;
}

void TimelineDockWidget::jumpToIntermediateFrame(bool forward) {
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    
    const auto range = scene->getFrameRange();
    const int currentFrame = scene->anim_getCurrentAbsFrame();
    const int totalFrames = range.fMax - range.fMin;
    const int quarterFrame = range.fMin + qRound(totalFrames * 0.25);
    const int middleFrame = range.fMin + qRound(totalFrames * 0.5);
    const int threeQuarterFrame = range.fMin + qRound(totalFrames * 0.75);
    
    if (forward) {
        if (currentFrame < quarterFrame) {
            scene->anim_setAbsFrame(quarterFrame);
        } else if (currentFrame < middleFrame) {
            scene->anim_setAbsFrame(middleFrame);
        } else if (currentFrame < threeQuarterFrame) {
            scene->anim_setAbsFrame(threeQuarterFrame);
        } else {
            scene->anim_setAbsFrame(range.fMax);
        }
    } else {
        if (currentFrame > threeQuarterFrame) {
            scene->anim_setAbsFrame(threeQuarterFrame);
        } else if (currentFrame > middleFrame) {
            scene->anim_setAbsFrame(middleFrame);
        } else if (currentFrame > quarterFrame) {
            scene->anim_setAbsFrame(quarterFrame);
        } else {
            scene->anim_setAbsFrame(range.fMin);
        }
    }
    mDocument.actionFinished();
}

void TimelineDockWidget::stepPreview()
{
    const auto scene = *mDocument.fActiveScene;
    if (!scene) { return; }
    int currentFrame = scene->anim_getCurrentAbsFrame();
    int nextFrame = currentFrame + 1;

    if (scene->getFrameIn().enabled && currentFrame < scene->getFrameIn().frame) {
        nextFrame = scene->getFrameIn().frame;
    }

    int frameOut = scene->getFrameRange().fMax;
    if (scene->getFrameOut().enabled) {
        frameOut = scene->getFrameOut().frame;
    }

    if (nextFrame > frameOut) {
        if (mLoopButton->isChecked()) {
            nextFrame = scene->getFrameRange().fMin;
            if (scene->getFrameIn().enabled) {
                nextFrame = scene->getFrameIn().frame;
            }
        } else {
            mStepPreviewTimer->stop();
            previewFinished();
            return;
        }
    }
    scene->anim_setAbsFrame(nextFrame);
    mDocument.actionFinished();
}
