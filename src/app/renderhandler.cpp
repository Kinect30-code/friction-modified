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

#include "renderhandler.h"
#include "videoencoder.h"
#include "memoryhandler.h"
#include "Private/Tasks/taskscheduler.h"
#include "canvas.h"
#include "Sound/soundcomposition.h"
#include "CacheHandlers/soundcachecontainer.h"
#include "CacheHandlers/sceneframecontainer.h"
#include "Private/document.h"
#include "simpletask.h"

#include <QSignalBlocker>

namespace {

constexpr int kPreviewCacheTrimMaxFramesPerPass = 24;
constexpr qreal kPreviewSteadyBufferSeconds = 0.75;

}

RenderHandler* RenderHandler::sInstance = nullptr;

RenderHandler::RenderHandler(Document &document,
                             AudioHandler& audioHandler,
                             VideoEncoder& videoEncoder,
                             MemoryHandler& memoryHandler) :
    mDocument(document),
    mAudioHandler(audioHandler),
    mVideoEncoder(videoEncoder) {
    Q_ASSERT(!sInstance);
    sInstance = this;

    connect(&memoryHandler, &MemoryHandler::allMemoryUsed,
            this, &RenderHandler::outOfMemory);

    mPreviewFPSTimer = new QTimer(this);
    mPreviewFPSTimer->setTimerType(Qt::PreciseTimer);
    connect(mPreviewFPSTimer, &QTimer::timeout,
            this, &RenderHandler::nextPreviewFrame);
    connect(mPreviewFPSTimer, &QTimer::timeout,
            this, &RenderHandler::audioPushTimerExpired);
    connect(audioHandler.audioOutput(), &QAudioOutput::notify,
            this, &RenderHandler::audioPushTimerExpired);

    const auto vidEmitter = videoEncoder.getEmitter();
//    connect(vidEmitter, &VideoEncoderEmitter::encodingStarted,
//            this, &SceneWindow::leaveOnlyInterruptionButtonsEnabled);
    connect(vidEmitter, &VideoEncoderEmitter::encodingFinished,
            this, &RenderHandler::interruptOutputRendering);
    connect(vidEmitter, &VideoEncoderEmitter::encodingInterrupted,
            this, &RenderHandler::interruptOutputRendering);
    connect(vidEmitter, &VideoEncoderEmitter::encodingFailed,
            this, &RenderHandler::interruptOutputRendering);
    connect(vidEmitter, &VideoEncoderEmitter::encodingStartFailed,
            this, &RenderHandler::interruptOutputRendering);
}

void RenderHandler::renderFromSettings(RenderInstanceSettings * const settings) {
    if(renderingPreviewValue()) {
        interruptPreviewRendering();
    }
    if(previewStateValue() == PreviewState::playing ||
       previewStateValue() == PreviewState::paused) {
        stopPreview();
    }

    setCurrentScene(settings->getTargetCanvas());
    if(!mCurrentScene) return;
    mCurrentScene->clearPreviewDisplayFrame();
    if(VideoEncoder::sStartEncoding(settings)) {
        mSavedCurrentFrame = mCurrentScene->getCurrentFrame();
        mSavedResolutionFraction = mCurrentScene->getResolution();

        mCurrentRenderSettings = settings;
        const auto &renderSettings = settings->getRenderSettings();
        setInternalFrameAction(renderSettings.fMinFrame);

        const qreal resolutionFraction = renderSettings.fResolution;
        mMinRenderFrame = renderSettings.fMinFrame;
        mMaxRenderFrame = renderSettings.fMaxFrame;
        const qreal fps = mCurrentScene->getFps();
        mMaxSoundSec = qFloor(mMaxRenderFrame/fps);

        const auto nextFrameFunc = [this]() {
            nextSaveOutputFrame();
        };
        TaskScheduler::sSetTaskUnderflowFunc(nextFrameFunc);
        TaskScheduler::sSetAllTasksFinishedFunc(nextFrameFunc);

        mCurrentRenderFrame = renderSettings.fMinFrame;
        mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};

        mCurrentEncodeFrame = mCurrentRenderFrame;
        mFirstEncodeSoundSecond = qFloor(mCurrentRenderFrame/fps);
        mCurrentEncodeSoundSecond = mFirstEncodeSoundSecond;
        if(!VideoEncoder::sEncodeAudio())
            mMaxSoundSec = mCurrentEncodeSoundSecond - 1;
        mCurrentScene->setMinFrameUseRange(mCurrentRenderFrame);
        mCurrentSoundComposition->setMinFrameUseRange(mCurrentRenderFrame);
        mCurrentSoundComposition->scheduleFrameRange({mCurrentRenderFrame,
                                                      mCurrentRenderFrame});
        mCurrentScene->anim_setAbsFrame(mCurrentRenderFrame);
        mCurrentScene->setOutputRendering(true);
        TaskScheduler::instance()->setAlwaysQue(true);
        //fitSceneToSize();
        if(!isZero6Dec(mSavedResolutionFraction - resolutionFraction)) {
            mCurrentScene->setResolution(resolutionFraction);
            mDocument.actionFinished();
        } else {
            nextCurrentRenderFrame();
            if(TaskScheduler::sAllQuedCpuTasksFinished()) {
                nextSaveOutputFrame();
            }
        }
    }
}

void RenderHandler::setPreviewFrame(const int &frame)
{
    const int previousFrame = currentPreviewFrameValue();
    setCurrentPreviewFrameValue(frame);
    if(mCurrentScene) {
        mCurrentScene->setPreviewDisplayFrame(frame);
    }
    if(previewStateValue() == PreviewState::paused &&
       frame != previousFrame) {
        setPreviewPlayedRangeValue({frame, frame});
    }
}

void RenderHandler::cacheAroundFrame(const int frame)
{
    if(!eSettings::instance().fPreviewCache) return;

    setCurrentScene(mDocument.fActiveScene);
    if(!mCurrentScene) return;
    if(mCurrentRenderSettings || mCurrentScene->isOutputRendering()) {
        return;
    }

    const auto state = previewStateValue();
    if(state == PreviewState::playing) {
        return;
    }

    if(state == PreviewState::paused) {
        ensurePreviewWindowQueued(frame);
        return;
    }

    auto timingState = previewTimingStateValue();

    const auto fIn = mCurrentScene->getFrameIn();
    const auto fOut = mCurrentScene->getFrameOut();
    const int cacheSpanForward = qMax(24, qRound(mCurrentScene->getFps()*2));
    const int cacheSpanBackward = qMax(12, qRound(mCurrentScene->getFps()));

    int minFrame = qMax(mCurrentScene->getMinFrame(), frame - cacheSpanBackward);
    int maxFrame = qMin(mCurrentScene->getMaxFrame(), frame + cacheSpanForward);

    if(fIn.enabled) {
        minFrame = qMax(minFrame, fIn.frame);
    }
    if(fOut.enabled) {
        maxFrame = qMin(maxFrame, fOut.frame);
    }
    if(maxFrame < minFrame) return;

    const int firstMissing = firstMissingUsablePreviewFrameAtOrAfter(minFrame, maxFrame);
    timingState.fBackgroundCacheVisibleFrame = frame;

    if(firstMissing > maxFrame) {
        setPreviewTimingState(timingState);
        applyPreviewWindowRange({minFrame, maxFrame}, false);
        stopBackgroundCaching();
        return;
    }

    if(!timingState.fBackgroundCaching) {
        const auto nextFrameFunc = [this]() {
            nextPreviewRenderFrame();
        };
        TaskScheduler::sSetTaskUnderflowFunc(nextFrameFunc);
        TaskScheduler::sSetAllTasksFinishedFunc(nextFrameFunc);
        timingState.fBackgroundCaching = true;
        setPreviewTimingState(timingState);
        setRenderingPreview(true);
    } else {
        setPreviewTimingState(timingState);
    }

    mMinRenderFrame = minFrame;
    mMaxRenderFrame = maxFrame;
    if(mCurrentRenderFrame < mMinRenderFrame - 1 ||
       mCurrentRenderFrame > mMaxRenderFrame ||
       mCurrentRenderFrame >= firstMissing) {
        mCurrentRenderFrame = qMax(mMinRenderFrame - 1, firstMissing - 1);
        mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};
    }

    ensurePreviewWindowQueued(frame);
}

void RenderHandler::setLoop(const bool loop) {
    mLoop = loop;
}

void RenderHandler::setFrameAction(const int frame) {
    if(mCurrentScene) mCurrentScene->anim_setAbsFrame(frame);
    mDocument.scheduleInteractiveScenesUpdate();
}

void RenderHandler::setInternalFrameAction(const int frame) {
    if(!mCurrentScene) return;
    mCurrentScene->anim_setAbsFrame(frame);
    SimpleTask::sProcessAll();
    TaskScheduler::instance()->queTasks();
}

void RenderHandler::setCurrentScene(Canvas * const scene) {
    if(mCurrentScene != scene) {
        setQueuedPreviewWindowValue({0, -1}, false);
        setPinnedPreviewWindowValue({0, -1}, false);
        setPreviewPlayedRangeValue({0, -1});
    }
    mCurrentScene = scene;
    mCurrentSoundComposition = scene ? scene->getSoundComposition() : nullptr;
}

bool RenderHandler::applyPreviewWindowRange(const FrameRange &range,
                                            bool scheduleAudio)
{
    if(!mCurrentScene || !range.isValid()) return false;
    bool queuedPreviewWindowValid = false;
    const FrameRange queuedPreviewWindow =
            queuedPreviewWindowValue(&queuedPreviewWindowValid);
    const bool rangeChanged =
            !queuedPreviewWindowValid || queuedPreviewWindow != range;

    syncPreviewUseRange(range);
    if(mCurrentSoundComposition && scheduleAudio && rangeChanged) {
        mCurrentSoundComposition->scheduleFrameRange(range);
    }

    if(!rangeChanged) {
        return false;
    }

    setQueuedPreviewWindowValue(range, true);
    trimPreviewCaches(range);
    return true;
}

void RenderHandler::nextCurrentRenderFrame() {
    int newCurrentRenderFrame = firstMissingUsablePreviewFrameAtOrAfter(
                mCurrentRenderFrame + 1, mMaxRenderFrame);
    const bool allDone = newCurrentRenderFrame > mMaxRenderFrame;
    newCurrentRenderFrame = qMin(mMaxRenderFrame, newCurrentRenderFrame);
    const FrameRange newSoundRange = {mCurrentRenderFrame, newCurrentRenderFrame};
    if(mCurrentSoundComposition) {
        mCurrentSoundComposition->scheduleFrameRange(newSoundRange);
    }

    mCurrentRenderFrame = newCurrentRenderFrame;
    mCurrRenderRange.fMax = mCurrentRenderFrame;
    if(allDone) {
        SimpleTask::sProcessAll();
        TaskScheduler::instance()->queTasks();
    }
    else {
        const auto state = previewStateValue();
        const auto timingState = previewTimingStateValue();
        const bool outputRendering =
                mCurrentScene && mCurrentScene->isOutputRendering();
        const bool keepVisiblePreviewFrame =
                !outputRendering &&
                (state == PreviewState::playing ||
                 state == PreviewState::paused ||
                 timingState.fBackgroundCaching);
        if(keepVisiblePreviewFrame) {
            bool queuedPreviewWindowValid = false;
            const FrameRange queuedPreviewWindow =
                    queuedPreviewWindowValue(&queuedPreviewWindowValid);
            if(queuedPreviewWindowValid) {
                syncPreviewUseRange(queuedPreviewWindow);
            }
        } else {
            mCurrentScene->setMaxFrameUseRange(newCurrentRenderFrame);
            if(mCurrentSoundComposition) {
                mCurrentSoundComposition->setMaxFrameUseRange(newCurrentRenderFrame);
            }
        }
        if(keepVisiblePreviewFrame) {
            const int visibleFrame =
                    state == PreviewState::playing ||
                    state == PreviewState::paused ?
                        currentPreviewFrameValue() :
                        timingState.fBackgroundCacheVisibleFrame;
            const QSignalBlocker blocker(mCurrentScene);
            setInternalFrameAction(mCurrentRenderFrame);
            ensureSceneFrameQueuedForCurrentRenderFrame();
            const auto visibleCont =
                    mCurrentScene->getSceneFramesHandler().
                    sharedAtFrame<SceneFrameContainer>(visibleFrame);
            mCurrentScene->anim_setAbsFrame(visibleFrame);
            if(visibleCont && visibleCont->storesDataInMemory()) {
                mCurrentScene->setSceneFrame(visibleCont);
            } else {
                mCurrentScene->scheduleUpdate();
            }
        } else {
            setInternalFrameAction(mCurrentRenderFrame);
            ensureSceneFrameQueuedForCurrentRenderFrame();
        }
    }
}

void RenderHandler::setPreviewState(const PreviewState state)
{
    const auto currentState = previewStateValue();
    if (currentState == state) { return; }
    const bool keepBackgroundPreviewRendering =
            (state == PreviewState::playing ||
             state == PreviewState::paused) &&
            eSettings::instance().fPreviewCache &&
            renderingPreviewValue();
    setRenderingPreview(keepBackgroundPreviewRendering);
    setPreviewing(state == PreviewState::playing);
    {
        QWriteLocker locker(&mPreviewStateLock);
        mPreviewState = state;
    }
}

void RenderHandler::interruptPreview() {
    if(renderingPreviewValue()) interruptPreviewRendering();
    else if(previewStateValue() == PreviewState::playing ||
            previewStateValue() == PreviewState::paused) {
        stopPreview();
    }
}

void RenderHandler::outOfMemory() {
    if(renderingPreviewValue()) {
        if(backgroundCachingValue()) {
            stopBackgroundCaching();
            return;
        }
        playPreview();
    }
}

void RenderHandler::setRenderingPreview(const bool rendering) {
    {
        QWriteLocker locker(&mPreviewStateLock);
        mRenderingPreview = rendering;
    }
    const bool backgroundCaching = backgroundCachingValue();
    const bool interactivePreviewState =
            previewStateValue() == PreviewState::playing ||
            previewStateValue() == PreviewState::paused;
    const bool blockCanvasEditing =
            rendering && !backgroundCaching && !interactivePreviewState;
    if(mCurrentScene) mCurrentScene->setRenderingPreview(blockCanvasEditing);
    const bool aggressiveQueueing =
            rendering && !interactivePreviewState && !backgroundCaching;
    TaskScheduler::instance()->setAlwaysQue(aggressiveQueueing);
}

void RenderHandler::setPreviewing(const bool previewing) {
    {
        QWriteLocker locker(&mPreviewStateLock);
        mPreviewing = previewing;
    }
    if(mCurrentScene) mCurrentScene->setPreviewing(previewing);
}

void RenderHandler::interruptPreviewRendering() {
    TaskScheduler::sClearAllFinishedFuncs();
    if(backgroundCachingValue()) {
        stopBackgroundCaching();
        return;
    }
    stopPreview();
}

void RenderHandler::interruptOutputRendering() {
    if(mCurrentScene) mCurrentScene->setOutputRendering(false);
    TaskScheduler::instance()->setAlwaysQue(false);
    TaskScheduler::sClearAllFinishedFuncs();
    stopPreview();
}

void RenderHandler::stopPreview() {
    if(mCurrentScene) {
        bool queuedPreviewWindowValid = false;
        const FrameRange queuedPreviewWindow =
                queuedPreviewWindowValue(&queuedPreviewWindowValid);
        const FrameRange previewPlayedRange = previewPlayedRangeValue();
        if(eSettings::instance().fPreviewCache && queuedPreviewWindowValid) {
            FrameRange pinnedWindow = queuedPreviewWindow;
            if(previewPlayedRange.isValid()) {
                pinnedWindow += previewPlayedRange;
            }
            setPinnedPreviewWindowValue(pinnedWindow, true);
            mCurrentScene->setMinFrameUseRange(pinnedWindow.fMin);
            mCurrentScene->setMaxFrameUseRange(pinnedWindow.fMax);
            if(mCurrentSoundComposition) {
                mCurrentSoundComposition->setMinFrameUseRange(pinnedWindow.fMin);
                mCurrentSoundComposition->setMaxFrameUseRange(pinnedWindow.fMax);
            }
        } else {
            mCurrentScene->clearUseRange();
            if(mCurrentSoundComposition) {
                mCurrentSoundComposition->clearUseRange();
            }
            setPinnedPreviewWindowValue({0, -1}, false);
        }
        setQueuedPreviewWindowValue({0, -1}, false);
        setPreviewPlayedRangeValue({0, -1});
        mCurrentScene->clearPreviewDisplayFrame();
        int targetFrame = mSavedCurrentFrame;
        const auto frameState = previewFrameStateValue();
        if (previewStateValue() == PreviewState::playing ||
            previewStateValue() == PreviewState::paused) {
            targetFrame = qBound(frameState.fMinFrame,
                                 frameState.fCurrentFrame,
                                 frameState.fMaxFrame);
        }
        setFrameAction(targetFrame);
        mCurrentScene->scheduleUpdate();
    }

    mPreviewFPSTimer->stop();
    mPreviewTickClock.invalidate();
    stopAudio();
    auto timingState = previewTimingStateValue();
    timingState.fFrameAccumulator = 0;
    timingState.fPlaybackRate = 1;
    timingState.fWaitingForCache = false;
    timingState.fAudioNeedsRestart = false;
    setPreviewTimingState(timingState);
    if(!mCurrentRenderSettings) {
        restorePreviewResolution();
    }
    emit previewFinished();
    setPreviewState(PreviewState::stopped);
}

void RenderHandler::pausePreview() {
    if(previewStateValue() == PreviewState::playing) {
        stopAudio();
        mPreviewFPSTimer->stop();
        mPreviewTickClock.invalidate();
        emit previewPaused();
        setPreviewState(PreviewState::paused);
    }
}

void RenderHandler::resumePreview() {
    if(previewStateValue() == PreviewState::paused) {
        if(mCurrentScene) {
            mCurrentScene->setPreviewDisplayFrame(currentPreviewFrameValue());
        }
        startAudio();
        mPreviewTickClock.invalidate();
        // Clear any stale waiting-for-cache flag from before the pause so
        // the first tick after resume doesn't immediately re-enter the stall.
        auto timingState = previewTimingStateValue();
        timingState.fWaitingForCache = false;
        timingState.fFrameAccumulator = 0;
        setPreviewTimingState(timingState);
        mPreviewFPSTimer->start();
        emit previewBeingPlayed();
        setPreviewState(PreviewState::playing);
    }
}

void RenderHandler::playPreview() {
    if(!mCurrentScene) {
        setCurrentScene(mDocument.fActiveScene);
    }
    if(!mCurrentScene) return;
    if(previewStateValue() == PreviewState::playing) {
        return;
    }
    if(previewStateValue() == PreviewState::paused) {
        const int requestedFrame = mCurrentScene->getCurrentFrame();
        if(requestedFrame != currentPreviewFrameValue()) {
            setPreviewFrame(requestedFrame);
        }
        resumePreview();
        return;
    }
    if(backgroundCachingValue()) {
        stopBackgroundCaching();
    }

    const auto sceneRange = mCurrentScene->getFrameRange();
    const int clampedCurrentFrame = qBound(sceneRange.fMin,
                                           mCurrentScene->getCurrentFrame(),
                                           sceneRange.fMax);
    if(clampedCurrentFrame != mCurrentScene->getCurrentFrame()) {
        mCurrentScene->anim_setAbsFrame(clampedCurrentFrame);
    }

    if(!renderingPreviewValue()) {
        mSavedCurrentFrame = mCurrentScene->getCurrentFrame();
        auto timingState = previewTimingStateValue();
        timingState.fBaseResolution = mCurrentScene->getResolution();
        setPreviewTimingState(timingState);

        const auto fIn = mCurrentScene->getFrameIn();
        const auto fOut = mCurrentScene->getFrameOut();
        mMinRenderFrame = mLoop ?
                    (fIn.enabled ? fIn.frame : mCurrentScene->getMinFrame()) - 1 :
                    (fIn.enabled ? fIn.frame : mSavedCurrentFrame);
        mMaxRenderFrame = fOut.enabled ?
                    fOut.frame :
                    mCurrentScene->getMaxFrame();
        mCurrentRenderFrame = mMinRenderFrame;
        mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};
        mCurrentScene->setMinFrameUseRange(mCurrentRenderFrame);
        if(mCurrentSoundComposition) {
            mCurrentSoundComposition->setMinFrameUseRange(mCurrentRenderFrame);
        }
    }

    const auto fIn = mCurrentScene->getFrameIn();
    const auto fOut = mCurrentScene->getFrameOut();
    const int savedFrame = mCurrentScene->getCurrentFrame();
    const int maxPreviewFrame = fOut.enabled ?
                fOut.frame :
                mCurrentScene->getMaxFrame();
    const int minPreviewFrame = fIn.enabled ?
                (fIn.frame < savedFrame && savedFrame < maxPreviewFrame ?
                     savedFrame : fIn.frame) :
                savedFrame;
    if(minPreviewFrame > maxPreviewFrame) return;
    const int boundedMinPreviewFrame = mLoop ?
                (fIn.enabled ? fIn.frame : mCurrentScene->getMinFrame()) :
                minPreviewFrame;
    setPreviewFrameState(boundedMinPreviewFrame, maxPreviewFrame, minPreviewFrame);
    setPreviewPlayedRangeValue({minPreviewFrame, minPreviewFrame});
    mCurrentScene->setPreviewDisplayFrame(currentPreviewFrameValue());

    if(!eSettings::instance().fPreviewCache) {
        TaskScheduler::sClearAllFinishedFuncs();
    } else {
        const auto nextFrameFunc = [this]() {
            nextPreviewRenderFrame();
        };
        TaskScheduler::sSetTaskUnderflowFunc(nextFrameFunc);
        TaskScheduler::sSetAllTasksFinishedFunc(nextFrameFunc);
    }

    ensurePreviewWindowQueued(currentPreviewFrameValue());
    const auto currentPreviewCont =
            mCurrentScene->getSceneFramesHandler().
            sharedAtFrame<SceneFrameContainer>(currentPreviewFrameValue());
    if(previewFrameReady(currentPreviewFrameValue())) {
        mCurrentScene->setSceneFrame(currentPreviewFrameValue());
    } else if(currentPreviewCont &&
              sceneFrameMatchesCurrentPreview(currentPreviewCont.get()) &&
              currentPreviewCont->hasRecoverableData()) {
        mCurrentScene->setLoadingSceneFrame(currentPreviewCont,
                                            currentPreviewFrameValue());
    } else {
        mCurrentScene->anim_setAbsFrame(currentPreviewFrameValue());
    }

    setPreviewState(PreviewState::playing);
    activateInteractivePreviewCaching();
    ensurePreviewWindowQueued(currentPreviewFrameValue());
    startAudio();

    mPreviewTickClock.invalidate();
    auto timingState = previewTimingStateValue();
    timingState.fNominalIntervalMs = qMax(1, qRound(1000/mCurrentScene->getFps()));
    timingState.fTickIntervalMs = qMax(1, qMin(16, timingState.fNominalIntervalMs));
    timingState.fFrameAccumulator = 0;
    timingState.fPlaybackRate = 1;
    timingState.fWaitingForCache = false;
    timingState.fAudioPausedForCache = false;
    timingState.fAudioNeedsRestart = false;
    timingState.fRenderCursorRetargeted = false;
    setPreviewTimingState(timingState);
    updatePreviewPlaybackRate(currentPreviewFrameValue() + 1);
    mPreviewFPSTimer->setInterval(timingState.fTickIntervalMs);
    mPreviewFPSTimer->start();
    emit previewBeingPlayed();
    mCurrentScene->scheduleUpdate();
}

void RenderHandler::nextPreviewRenderFrame() {
    if(!renderingPreviewValue()) return;
    if((previewStateValue() == PreviewState::playing ||
        previewStateValue() == PreviewState::paused) &&
       previewHasBufferedAhead(currentPreviewFrameValue(), previewSteadyBufferFrames())) {
        suspendInteractivePreviewCaching();
        return;
    }
    if(mCurrentRenderFrame >= mMaxRenderFrame) {
        if(backgroundCachingValue()) {
            stopBackgroundCaching();
        } else {
            suspendInteractivePreviewCaching();
        }
    } else {
        nextCurrentRenderFrame();
        if(TaskScheduler::sAllTasksFinished()) {
            QTimer::singleShot(0, this, [this]() {
                nextPreviewRenderFrame();
            });
        }
    }
}

void RenderHandler::nextPreviewFrame() {
    if(!mCurrentScene) return;
    if(!mPreviewTickClock.isValid()) {
        mPreviewTickClock.start();
        ensurePreviewWindowQueued(currentPreviewFrameValue() + 1);
        return;
    }

    const qint64 elapsedMs = mPreviewTickClock.restart();
    const qreal fps = mCurrentScene->getFps();
    updatePreviewPlaybackRate(currentPreviewFrameValue() + 1);
    auto timingState = previewTimingStateValue();
    const auto frameState = previewFrameStateValue();
    timingState.fFrameAccumulator += qMax<qreal>(0.0, elapsedMs)*
            fps*timingState.fPlaybackRate/1000.0;
    if(timingState.fFrameAccumulator < 1.0) {
        setPreviewTimingState(timingState);
        ensurePreviewWindowQueued(currentPreviewFrameValue() + 1);
        return;
    }

    timingState.fFrameAccumulator =
            qMin<qreal>(timingState.fFrameAccumulator, 2.0);
    const int nextFrame = frameState.fCurrentFrame + 1;
    if(nextFrame > frameState.fMaxFrame) {
        timingState.fFrameAccumulator = 0;
        setPreviewTimingState(timingState);
        if(mLoop) {
            const int loopStartFrame = frameState.fMinFrame;
            ensurePreviewWindowQueued(loopStartFrame);
            const int warmedMaxFrame =
                    qMin(frameState.fMaxFrame,
                         loopStartFrame + previewSteadyBufferFrames() - 1);
            warmPreviewFramesInMemory({loopStartFrame, warmedMaxFrame},
                                      previewSteadyBufferFrames());
            const int savedFrame = frameState.fCurrentFrame;
            setCurrentPreviewFrameValue(loopStartFrame - 1);
            timingState.fFrameAccumulator = 1.0;
            setPreviewTimingState(timingState);
            mPreviewTickClock.restart();
            nextPreviewFrame();
            if (currentPreviewFrameValue() == loopStartFrame - 1) {
                setCurrentPreviewFrameValue(savedFrame);
            } else {
                stopAudio();
                startAudio();
            }
        } else stopPreview();
    } else {
        const bool wasWaitingForCache = timingState.fWaitingForCache;
        ensurePreviewWindowQueued(nextFrame);
        const auto cachedFrame = mCurrentScene->getSceneFramesHandler().
                sharedAtFrame<SceneFrameContainer>(nextFrame);
        const bool exactFrameReady =
                cachedFrame &&
                sceneFrameMatchesCurrentPreview(cachedFrame.get()) &&
                cachedFrame->storesDataInMemory();
        if(!exactFrameReady) {
            // If a cached frame exists but its stateId no longer matches the
            // current scene state, the scene changed after caching started.
            // The render system may believe all frames are done (mCurrentRenderFrame
            // >= mMaxRenderFrame) while the cache is stale — a deadlock.
            // Detect this and force a re-render by resetting the render cursor.
            if(cachedFrame && !sceneFrameMatchesCurrentPreview(cachedFrame.get())) {
                invalidateSceneFrameIfStale(nextFrame);
                // Stale cache — scene state changed. Reset render cursor so
                // ensurePreviewWindowQueued will re-activate caching.
                mCurrentRenderFrame = qMax(mMinRenderFrame, nextFrame - 1);
                mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};
                // Re-run ensurePreviewWindowQueued now that the cursor is reset
                // so it can immediately re-activate interactive caching.
                ensurePreviewWindowQueued(nextFrame);
            } else if(cachedFrame &&
                      sceneFrameMatchesCurrentPreview(cachedFrame.get()) &&
                      !cachedFrame->storesDataInMemory() &&
                      cachedFrame->hasRecoverableData()) {
                // Register the exact frame we're waiting on so tmp-load
                // completion can promote it to the visible scene frame.
                mCurrentScene->setLoadingSceneFrame(cachedFrame, nextFrame);
            } else if(cachedFrame &&
                      sceneFrameMatchesCurrentPreview(cachedFrame.get()) &&
                      !cachedFrame->hasRecoverableData()) {
                mCurrentScene->getSceneFramesHandler().remove(cachedFrame->getRange());
                mCurrentRenderFrame = qMax(mMinRenderFrame, nextFrame - 1);
                mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};
                ensurePreviewWindowQueued(nextFrame);
                mCurrentScene->setLoadingSceneFrame(nullptr);
            }
            timingState.fWaitingForCache = true;
            timingState.fFrameAccumulator =
                    qMin<qreal>(timingState.fFrameAccumulator, 1.0);
            setPreviewTimingState(timingState);
            if(!wasWaitingForCache) {
                mCurrentScene->scheduleUpdate();
            }
            return;
        }
        if(timingState.fWaitingForCache) {
            timingState.fWaitingForCache = false;
            // Reset the tick clock so accumulated wait time doesn't cause
            // frame skips when playback resumes after a cache stall.
            mPreviewTickClock.restart();
        }
        timingState.fFrameAccumulator =
                qMax<qreal>(0.0, timingState.fFrameAccumulator - 1.0);
        setPreviewTimingState(timingState);
        setCurrentPreviewFrameValue(nextFrame);
        mCurrentScene->setPreviewDisplayFrame(nextFrame);
        appendPreviewPlayedFrameValue(nextFrame);
        bool queuedPreviewWindowValid = false;
        const FrameRange queuedPreviewWindow =
                queuedPreviewWindowValue(&queuedPreviewWindowValid);
        if(queuedPreviewWindowValid) {
            syncPreviewUseRange(queuedPreviewWindow);
        }
        mCurrentScene->setSceneFrame(currentPreviewFrameValue());
        updatePreviewPlaybackRate(currentPreviewFrameValue() + 1);
        emit mCurrentScene->currentFrameChanged(currentPreviewFrameValue());
    }
}

void RenderHandler::finishEncoding() {
    TaskScheduler::sClearAllFinishedFuncs();
    mCurrentRenderSettings = nullptr;
    mCurrentScene->setOutputRendering(false);
    TaskScheduler::instance()->setAlwaysQue(false);
    setFrameAction(mSavedCurrentFrame);
    if(!isZero4Dec(mSavedResolutionFraction - mCurrentScene->getResolution())) {
        mCurrentScene->setResolution(mSavedResolutionFraction);
    }
    mCurrentSoundComposition->clearUseRange();
    setQueuedPreviewWindowValue({0, -1}, false);
    VideoEncoder::sFinishEncoding();
}

void RenderHandler::nextSaveOutputFrame() {
    auto &soundCacheHandler = mCurrentSoundComposition->getCacheHandler();
    const qreal fps = mCurrentScene->getFps();
    const int sampleRate = eSoundSettings::sSampleRate();
    const int minSample = qRound(mMinRenderFrame*sampleRate/fps);
    const int maxSample = qCeil((mMaxRenderFrame + 1)*sampleRate/fps) - 1;
    while(mCurrentEncodeSoundSecond <= mMaxSoundSec) {
        const auto cont = soundCacheHandler.sharedAtFrame<SoundCacheContainer>(
                    mCurrentEncodeSoundSecond);
        if(!cont) {
            mCurrentSoundComposition->scheduleSecondIfNeeded(
                        mCurrentEncodeSoundSecond);
            break;
        }
        if(!cont->hasRecoverableData()) {
            soundCacheHandler.remove(cont->getRange());
            mCurrentSoundComposition->scheduleSecondIfNeeded(
                        mCurrentEncodeSoundSecond);
            break;
        }
        if(!cont->storesDataInMemory()) {
            cont->scheduleLoadFromTmpFile();
            break;
        }
        const auto samples = cont->getSamples();
        if(!samples) {
            soundCacheHandler.remove(cont->getRange());
            mCurrentSoundComposition->scheduleSecondIfNeeded(
                        mCurrentEncodeSoundSecond);
            break;
        }
        SampleRange neededRange = samples->fSampleRange;
        if(mCurrentEncodeSoundSecond == mFirstEncodeSoundSecond) {
            neededRange.fMin = qMax(neededRange.fMin, minSample);
        }
        if(mCurrentEncodeSoundSecond == mMaxSoundSec) {
            neededRange.fMax = qMin(neededRange.fMax, maxSample);
        }

        if(!neededRange.isValid()) {
            mCurrentEncodeSoundSecond++;
            continue;
        }

        if(neededRange == samples->fSampleRange) {
            VideoEncoder::sAddCacheContainerToEncoder(
                        enve::make_shared<Samples>(samples));
        } else {
            VideoEncoder::sAddCacheContainerToEncoder(
                        samples->mid(neededRange));
        }
        mCurrentEncodeSoundSecond++;
    }
    if(mCurrentEncodeSoundSecond > mMaxSoundSec) VideoEncoder::sAllAudioProvided();

    auto &cacheHandler = mCurrentScene->getSceneFramesHandler();
    while(mCurrentEncodeFrame <= mMaxRenderFrame) {
        const auto cont = cacheHandler.sharedAtFrame<SceneFrameContainer>(
                    mCurrentEncodeFrame);
        if(!cont) {
            mCurrentRenderFrame = qMax(mMinRenderFrame, mCurrentEncodeFrame - 1);
            mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};
            break;
        }
        if(!sceneFrameMatchesCurrentPreview(cont.get()) ||
           !cont->hasRecoverableData()) {
            cacheHandler.remove(cont->getRange());
            mCurrentRenderFrame = qMax(mMinRenderFrame, mCurrentEncodeFrame - 1);
            mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};
            break;
        }
        if(!cont->storesDataInMemory()) {
            cont->scheduleLoadFromTmpFile();
            break;
        }
        const auto image = cont->requestImageCopy();
        if(!image) {
            cont->scheduleLoadFromTmpFile();
            break;
        }
        VideoEncoder::sAddCacheContainerToEncoder({image, cont->getRange()});
        mCurrentEncodeFrame = cont->getRangeMax() + 1;
    }

    if(mCurrentRenderSettings) {
        int reportedFrame = mCurrentRenderFrame;
        if(mCurrentEncodeFrame > mMinRenderFrame) {
            reportedFrame = qMax(reportedFrame, mCurrentEncodeFrame - 1);
        }
        reportedFrame = qBound(mMinRenderFrame, reportedFrame, mMaxRenderFrame);
        mCurrentRenderSettings->setCurrentRenderFrame(reportedFrame);
    }

    //mCurrentScene->renderCurrentFrameToOutput(*mCurrentRenderSettings);
    if(mCurrentRenderFrame >= mMaxRenderFrame) {
        if(mCurrentEncodeSoundSecond <= mMaxSoundSec) return;
        if(mCurrentEncodeFrame <= mMaxRenderFrame) return;
        TaskScheduler::sSetTaskUnderflowFunc(nullptr);
        Document::sInstance->actionFinished();
        if(TaskScheduler::sAllTasksFinished()) {
            finishEncoding();
        } else {
            TaskScheduler::sSetAllTasksFinishedFunc([this]() {
                finishEncoding();
            });
        }
    } else {
        nextCurrentRenderFrame();
        if(TaskScheduler::sAllTasksFinished()) {
            nextSaveOutputFrame();
        }
    }
}

void RenderHandler::startAudio() {
    auto timingState = previewTimingStateValue();
    if(!mCurrentSoundComposition || !mCurrentSoundComposition->hasAnySounds()) {
        if(timingState.fAudioActive) {
            stopAudio();
        }
        return;
    }
    if(timingState.fAudioActive) {
        mAudioHandler.stopAudio();
        if(mCurrentSoundComposition->isOpen()) {
            mCurrentSoundComposition->stop();
        }
    }
    mAudioHandler.startAudio();
    mCurrentSoundComposition->start(currentPreviewFrameValue());
    timingState.fAudioActive = true;
    setPreviewTimingState(timingState);
    audioPushTimerExpired();
}

void RenderHandler::stopAudio() {
    auto timingState = previewTimingStateValue();
    if(!timingState.fAudioActive &&
       (!mCurrentSoundComposition || !mCurrentSoundComposition->isOpen())) {
        return;
    }
    mAudioHandler.stopAudio();
    if(mCurrentSoundComposition && mCurrentSoundComposition->isOpen()) {
        mCurrentSoundComposition->stop();
    }
    timingState.fAudioActive = false;
    timingState.fAudioPausedForCache = false;
    setPreviewTimingState(timingState);
}

void RenderHandler::audioPushTimerExpired() {
    if(!previewTimingStateValue().fAudioActive || !mCurrentSoundComposition) {
        return;
    }
    if(!mCurrentSoundComposition->hasAnySounds()) {
        stopAudio();
        return;
    }
    while(auto request = mAudioHandler.dataRequest()) {
        const qint64 len = mCurrentSoundComposition->read(
                    request.fData, request.fSize);
        if(len <= 0) break;
        request.fSize = int(len);
        mAudioHandler.provideData(request);
    }
}

void RenderHandler::ensurePreviewWindowQueued(int anchorFrame) {
    if(!mCurrentScene) return;
    bool queuedPreviewWindowValid = false;
    const FrameRange queuedPreviewWindow =
            queuedPreviewWindowValue(&queuedPreviewWindowValid);
    const bool activePreview =
            previewStateValue() == PreviewState::playing ||
            previewStateValue() == PreviewState::paused;
    const int cacheSpanForward = activePreview ?
                qMax(48, qRound(mCurrentScene->getFps()*2.0)) :
                qMax(24, qRound(mCurrentScene->getFps()*1.5));
    const int cacheSpanBackward = activePreview ?
                qMax(12, qRound(mCurrentScene->getFps()*0.5)) :
                qMax(6, qRound(mCurrentScene->getFps()*0.25));
    int minFrame = qMax(mMinRenderFrame, anchorFrame - cacheSpanBackward);
    int maxFrame = qMin(mMaxRenderFrame, anchorFrame + cacheSpanForward);
    const auto fIn = mCurrentScene->getFrameIn();
    const auto fOut = mCurrentScene->getFrameOut();
    if(fIn.enabled) {
        minFrame = qMax(minFrame, fIn.frame);
    }
    if(fOut.enabled) {
        maxFrame = qMin(maxFrame, fOut.frame);
    }
    if(maxFrame < minFrame) {
        maxFrame = minFrame;
    }
    if(activePreview && queuedPreviewWindowValid) {
        const int keepForward = qMax(12, cacheSpanForward/3);
        const int keepBackward = qMax(6, cacheSpanBackward/2);
        const int requiredForwardBuffered =
                previewRequiredBufferFrames(anchorFrame, keepForward);
        const bool enoughForwardBuffered =
                requiredForwardBuffered <= 0 ||
                previewBufferedAheadFrames(anchorFrame,
                                           requiredForwardBuffered) >= requiredForwardBuffered;
        const bool enoughBackwardBuffered =
                anchorFrame - queuedPreviewWindow.fMin >= keepBackward;
        if(enoughForwardBuffered && enoughBackwardBuffered) {
            return;
        }
    }
    const int firstMissingUsable =
            firstMissingUsablePreviewFrameAtOrAfter(anchorFrame, maxFrame);
    const int firstUnready = activePreview ?
                firstUnreadyPreviewFrameAtOrAfter(anchorFrame, maxFrame) :
                firstMissingUsable;
    if(firstUnready <= maxFrame) {
        maxFrame = qMax(firstUnready + cacheSpanForward/2, anchorFrame);
        maxFrame = qMin(maxFrame, mMaxRenderFrame);
        if(fOut.enabled) {
            maxFrame = qMin(maxFrame, fOut.frame);
        }
    }

    if(renderingPreviewValue() && firstMissingUsable <= maxFrame) {
        const bool renderCursorOutsideWindow =
                mCurrentRenderFrame < minFrame || mCurrentRenderFrame > maxFrame;
        const bool renderCursorAheadOfNeed = mCurrentRenderFrame > firstMissingUsable;
        if(renderCursorOutsideWindow || renderCursorAheadOfNeed) {
            mCurrentRenderFrame = qMax(mMinRenderFrame, firstMissingUsable - 1);
            mCurrRenderRange.fMax = mCurrentRenderFrame;
            setPreviewRenderCursorRetargetedValue(true);
        }
    }
    if(activePreview && !renderingPreviewValue() && firstMissingUsable <= maxFrame) {
        mCurrentRenderFrame = qMax(mMinRenderFrame, firstMissingUsable - 1);
        mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};
        activateInteractivePreviewCaching();
    }

    if(applyPreviewWindowRange({minFrame, maxFrame}, true)) {
        mDocument.scheduleInteractiveScenesUpdate();
    }
    if(activePreview) {
        warmPreviewFramesInMemory({anchorFrame, maxFrame},
                                  previewSteadyBufferFrames());
    }

    if(previewRenderCursorRetargetedValue() && TaskScheduler::sAllTasksFinished()) {
        setPreviewRenderCursorRetargetedValue(false);
        QTimer::singleShot(0, this, [this]() {
            nextPreviewRenderFrame();
        });
    }
}

void RenderHandler::stopBackgroundCaching()
{
    if(!backgroundCachingValue()) return;
    auto timingState = previewTimingStateValue();
    timingState.fBackgroundCaching = false;
    timingState.fRenderCursorRetargeted = false;
    setPreviewTimingState(timingState);
    setQueuedPreviewWindowValue({0, -1}, false);
    TaskScheduler::sSetTaskUnderflowFunc(nullptr);
    TaskScheduler::sSetAllTasksFinishedFunc(nullptr);
    setRenderingPreview(false);
}

void RenderHandler::trimPreviewCaches(const FrameRange &activeRange) {
    if(!mCurrentScene || !activeRange.isValid()) {
        return;
    }
    if(previewStateValue() == PreviewState::playing ||
       previewStateValue() == PreviewState::paused) {
        return;
    }

    const int keepBackward = qMax(6, qRound(mCurrentScene->getFps()*0.5));
    const int keepForward = qMax(18, qRound(mCurrentScene->getFps()*1.0));
    FrameRange keepRange{
        qMax(mMinRenderFrame, activeRange.fMin - keepBackward),
        qMin(mMaxRenderFrame, activeRange.fMax + keepForward)
    };
    bool pinnedPreviewWindowValid = false;
    const FrameRange pinnedPreviewWindow =
            pinnedPreviewWindowValue(&pinnedPreviewWindowValid);
    if(pinnedPreviewWindowValid) {
        const FrameRange pinnedRange{
            qMax(mMinRenderFrame, pinnedPreviewWindow.fMin - keepBackward),
            qMin(mMaxRenderFrame, pinnedPreviewWindow.fMax + keepForward)
        };
        keepRange += pinnedRange;
    }
    mCurrentScene->freeUnusedSceneFramesOutsideRange(
                keepRange, kPreviewCacheTrimMaxFramesPerPass);
}

int RenderHandler::warmPreviewFramesInMemory(const FrameRange &range,
                                             int maxFrames) {
    if(!mCurrentScene || !range.isValid() || maxFrames <= 0) {
        return 0;
    }

    auto &cacheHandler = mCurrentScene->getSceneFramesHandler();
    int warmedFrames = 0;
    for(auto it = cacheHandler.begin();
        it != cacheHandler.end() && warmedFrames < maxFrames;
        ++it) {
        const auto cont = static_cast<SceneFrameContainer*>(it->second.get());
        if(!cont || !sceneFrameMatchesCurrentPreview(cont)) {
            continue;
        }
        const FrameRange overlap = cont->getRange()*range;
        if(!overlap.isValid()) {
            continue;
        }
        if(cont->storesDataInMemory()) {
            warmedFrames += overlap.span();
            continue;
        }
        if(cont->scheduleLoadFromTmpFile()) {
            warmedFrames += overlap.span();
        }
    }
    return warmedFrames;
}

bool RenderHandler::sceneFrameMatchesCurrentPreview(
        const SceneFrameContainer *cont) const {
    return cont &&
           mCurrentScene &&
           cont->fBoxState == mCurrentScene->currentStateId() &&
           isZero6Dec(cont->fResolution - mCurrentScene->getResolution());
}

int RenderHandler::firstMissingUsablePreviewFrameAtOrAfter(int frame,
                                                           int maxFrame) const {
    if(!mCurrentScene) {
        return frame;
    }
    const auto &cacheHandler = mCurrentScene->getSceneFramesHandler();
    for(int current = qMax(frame, mMinRenderFrame);
        current <= maxFrame;
        current++) {
        const auto cont = cacheHandler.atFrame<SceneFrameContainer>(current);
        if(!sceneFrameMatchesCurrentPreview(cont) ||
           !cont->hasRecoverableData()) {
            return current;
        }
    }
    return maxFrame + 1;
}

int RenderHandler::firstUnreadyPreviewFrameAtOrAfter(int frame,
                                                     int maxFrame) const {
    if(!mCurrentScene) {
        return frame;
    }
    for(int current = qMax(frame, mMinRenderFrame);
        current <= maxFrame;
        current++) {
        if(!previewFrameReady(current)) {
            return current;
        }
    }
    return maxFrame + 1;
}

bool RenderHandler::previewFrameReady(int frame) const {
    if(!mCurrentScene) {
        return false;
    }
    const auto cont = mCurrentScene->getSceneFramesHandler().
            atFrame<SceneFrameContainer>(frame);
    return cont && sceneFrameMatchesCurrentPreview(cont) &&
           cont->storesDataInMemory();
}

int RenderHandler::previewSteadyBufferFrames() const {
    if(!mCurrentScene) {
        return 0;
    }
    return qMax(12, qRound(mCurrentScene->getFps()*kPreviewSteadyBufferSeconds));
}

int RenderHandler::previewRequiredBufferFrames(int anchorFrame,
                                               int desiredFrames) const {
    if(!mCurrentScene || desiredFrames <= 0) {
        return 0;
    }
    const auto frameState = previewFrameStateValue();
    if(anchorFrame > frameState.fMaxFrame) {
        return 0;
    }
    return qMin(desiredFrames, frameState.fMaxFrame - anchorFrame + 1);
}

int RenderHandler::previewBufferedAheadFrames(int anchorFrame, int maxFrames) const {
    const auto frameState = previewFrameStateValue();
    if(!mCurrentScene || maxFrames <= 0 || anchorFrame > frameState.fMaxFrame) {
        return 0;
    }
    const int lastFrame = qMin(frameState.fMaxFrame, anchorFrame + maxFrames - 1);
    int bufferedFrames = 0;
    for(int frame = anchorFrame; frame <= lastFrame; frame++) {
        if(!previewFrameReady(frame)) {
            break;
        }
        bufferedFrames++;
    }
    return bufferedFrames;
}

bool RenderHandler::previewHasBufferedAhead(int anchorFrame, int targetFrames) const {
    bool queuedPreviewWindowValid = false;
    queuedPreviewWindowValue(&queuedPreviewWindowValid);
    if(!mCurrentScene || targetFrames <= 0 || !queuedPreviewWindowValid) {
        return false;
    }
    const int requiredFrames =
            previewRequiredBufferFrames(anchorFrame, targetFrames);
    if(requiredFrames <= 0) {
        return true;
    }
    return previewBufferedAheadFrames(anchorFrame, requiredFrames) >= requiredFrames;
}

qreal RenderHandler::previewPlaybackRateForBufferedFrames(int bufferedFrames,
                                                          int targetFrames) const {
    Q_UNUSED(bufferedFrames)
    Q_UNUSED(targetFrames)
    // AE-style preview should either advance at full rate or wait for cache.
    return 1.0;
}

void RenderHandler::updatePreviewPlaybackRate(int anchorFrame) {
    auto timingState = previewTimingStateValue();
    if(!mPreviewFPSTimer || timingState.fNominalIntervalMs <= 0) {
        return;
    }
    const int targetFrames =
            previewRequiredBufferFrames(anchorFrame, previewSteadyBufferFrames());
    const int bufferedFrames = previewBufferedAheadFrames(anchorFrame,
                                                          targetFrames);
    timingState.fPlaybackRate =
            previewPlaybackRateForBufferedFrames(bufferedFrames, targetFrames);
    setPreviewTimingState(timingState);
}

void RenderHandler::syncPreviewUseRange(const FrameRange &baseRange) {
    if(!mCurrentScene || !baseRange.isValid()) {
        return;
    }
    FrameRange retainedRange = baseRange;
    if(previewStateValue() == PreviewState::playing ||
       previewStateValue() == PreviewState::paused) {
        const FrameRange playedRange = previewPlayedRangeValue();
        if(playedRange.isValid()) {
            retainedRange += playedRange;
        }
    }
    mCurrentScene->setMinFrameUseRange(retainedRange.fMin);
    mCurrentScene->setMaxFrameUseRange(retainedRange.fMax);
    if(mCurrentSoundComposition) {
        mCurrentSoundComposition->setMinFrameUseRange(retainedRange.fMin);
        mCurrentSoundComposition->setMaxFrameUseRange(retainedRange.fMax);
    }
}

void RenderHandler::restorePreviewResolution() {
    if(!mCurrentScene) {
        return;
    }
    const auto timingState = previewTimingStateValue();
    if(!isZero6Dec(mCurrentScene->getResolution() - timingState.fBaseResolution)) {
        mCurrentScene->setResolution(timingState.fBaseResolution);
        mDocument.scheduleInteractiveScenesUpdate();
    }
}

void RenderHandler::activateInteractivePreviewCaching() {
    if(backgroundCachingValue() || !mCurrentScene) {
        return;
    }
    const auto nextFrameFunc = [this]() {
        nextPreviewRenderFrame();
    };
    TaskScheduler::sSetTaskUnderflowFunc(nextFrameFunc);
    TaskScheduler::sSetAllTasksFinishedFunc(nextFrameFunc);
    setRenderingPreview(true);
    if(TaskScheduler::sAllTasksFinished()) {
        QTimer::singleShot(0, this, [this]() {
            nextPreviewRenderFrame();
        });
    }
}

void RenderHandler::suspendInteractivePreviewCaching() {
    if(backgroundCachingValue() || !renderingPreviewValue()) {
        return;
    }
    TaskScheduler::sSetTaskUnderflowFunc(nullptr);
    TaskScheduler::sSetAllTasksFinishedFunc(nullptr);
    setRenderingPreview(false);
}

void RenderHandler::invalidateSceneFrameIfStale(int frame) {
    if(!mCurrentScene) {
        return;
    }

    auto &cacheHandler = mCurrentScene->getSceneFramesHandler();
    const auto cont = cacheHandler.atFrame<SceneFrameContainer>(frame);
    if(!cont) {
        return;
    }
    if(sceneFrameMatchesCurrentPreview(cont) && cont->hasRecoverableData()) {
        return;
    }

    cacheHandler.remove(cont->getRange());
    if(mCurrentScene->loadingSceneFrame() == cont) {
        mCurrentScene->setLoadingSceneFrame(nullptr);
    }
}

void RenderHandler::ensureSceneFrameQueuedForCurrentRenderFrame() {
    if(!mCurrentScene) {
        return;
    }

    invalidateSceneFrameIfStale(mCurrentRenderFrame);
    const auto cont = mCurrentScene->getSceneFramesHandler().
            atFrame<SceneFrameContainer>(mCurrentRenderFrame);
    if(cont && sceneFrameMatchesCurrentPreview(cont) &&
       cont->hasRecoverableData()) {
        return;
    }

    mCurrentScene->queRender(mCurrentRenderFrame, QMatrix());
}

PreviewState RenderHandler::previewStateValue() const {
    QReadLocker locker(&mPreviewStateLock);
    return mPreviewState;
}

int RenderHandler::currentPreviewFrameValue() const {
    QReadLocker locker(&mPreviewStateLock);
    return mCurrentPreviewFrame;
}

RenderHandler::PreviewFrameState RenderHandler::previewFrameStateValue() const {
    QReadLocker locker(&mPreviewStateLock);
    return {mCurrentPreviewFrame, mMinPreviewFrame, mMaxPreviewFrame};
}

bool RenderHandler::previewingValue() const {
    QReadLocker locker(&mPreviewStateLock);
    return mPreviewing;
}

bool RenderHandler::renderingPreviewValue() const {
    QReadLocker locker(&mPreviewStateLock);
    return mRenderingPreview;
}

void RenderHandler::setCurrentPreviewFrameValue(int frame) {
    QWriteLocker locker(&mPreviewStateLock);
    mCurrentPreviewFrame = frame;
}

void RenderHandler::setPreviewFrameState(int minFrame,
                                         int maxFrame,
                                         int currentFrame) {
    QWriteLocker locker(&mPreviewStateLock);
    mMinPreviewFrame = minFrame;
    mMaxPreviewFrame = maxFrame;
    mCurrentPreviewFrame = currentFrame;
}

RenderHandler::PreviewTimingState RenderHandler::previewTimingStateValue() const {
    QReadLocker locker(&mPreviewStateLock);
    return {mPreviewNominalIntervalMs,
            mPreviewTickIntervalMs,
            mPreviewFrameAccumulator,
            mPreviewPlaybackRate,
            mPreviewWaitingForCache,
            mPreviewAudioPausedForCache,
            mPreviewAudioNeedsRestart,
            mPreviewAudioActive,
            mPreviewRenderCursorRetargeted,
            mBackgroundCaching,
            mBackgroundCacheVisibleFrame,
            mPreviewBaseResolution};
}

void RenderHandler::setPreviewTimingState(const PreviewTimingState &state) {
    QWriteLocker locker(&mPreviewStateLock);
    mPreviewNominalIntervalMs = state.fNominalIntervalMs;
    mPreviewTickIntervalMs = state.fTickIntervalMs;
    mPreviewFrameAccumulator = state.fFrameAccumulator;
    mPreviewPlaybackRate = state.fPlaybackRate;
    mPreviewWaitingForCache = state.fWaitingForCache;
    mPreviewAudioPausedForCache = state.fAudioPausedForCache;
    mPreviewAudioNeedsRestart = state.fAudioNeedsRestart;
    mPreviewAudioActive = state.fAudioActive;
    mPreviewRenderCursorRetargeted = state.fRenderCursorRetargeted;
    mBackgroundCaching = state.fBackgroundCaching;
    mBackgroundCacheVisibleFrame = state.fBackgroundCacheVisibleFrame;
    mPreviewBaseResolution = state.fBaseResolution;
}

FrameRange RenderHandler::queuedPreviewWindowValue(bool *valid) const {
    QReadLocker locker(&mPreviewStateLock);
    if(valid) {
        *valid = mQueuedPreviewWindowValid;
    }
    return mQueuedPreviewWindow;
}

void RenderHandler::setQueuedPreviewWindowValue(const FrameRange &range,
                                                bool valid) {
    QWriteLocker locker(&mPreviewStateLock);
    mQueuedPreviewWindow = range;
    mQueuedPreviewWindowValid = valid;
}

FrameRange RenderHandler::pinnedPreviewWindowValue(bool *valid) const {
    QReadLocker locker(&mPreviewStateLock);
    if(valid) {
        *valid = mPinnedPreviewWindowValid;
    }
    return mPinnedPreviewWindow;
}

void RenderHandler::setPinnedPreviewWindowValue(const FrameRange &range,
                                                bool valid) {
    QWriteLocker locker(&mPreviewStateLock);
    mPinnedPreviewWindow = range;
    mPinnedPreviewWindowValid = valid;
}

FrameRange RenderHandler::previewPlayedRangeValue() const {
    QReadLocker locker(&mPreviewStateLock);
    return mPreviewPlayedRange;
}

void RenderHandler::setPreviewPlayedRangeValue(const FrameRange &range) {
    QWriteLocker locker(&mPreviewStateLock);
    mPreviewPlayedRange = range;
}

void RenderHandler::appendPreviewPlayedFrameValue(int frame) {
    QWriteLocker locker(&mPreviewStateLock);
    if(mPreviewPlayedRange.isValid()) {
        mPreviewPlayedRange += FrameRange{frame, frame};
    } else {
        mPreviewPlayedRange = {frame, frame};
    }
}

bool RenderHandler::backgroundCachingValue() const {
    QReadLocker locker(&mPreviewStateLock);
    return mBackgroundCaching;
}

bool RenderHandler::previewRenderCursorRetargetedValue() const {
    QReadLocker locker(&mPreviewStateLock);
    return mPreviewRenderCursorRetargeted;
}

void RenderHandler::setPreviewRenderCursorRetargetedValue(bool retargeted) {
    QWriteLocker locker(&mPreviewStateLock);
    mPreviewRenderCursorRetargeted = retargeted;
}
