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

#include <QSignalBlocker>

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
    setCurrentScene(settings->getTargetCanvas());
    if(VideoEncoder::sStartEncoding(settings)) {
        mSavedCurrentFrame = mCurrentScene->getCurrentFrame();
        mSavedResolutionFraction = mCurrentScene->getResolution();

        mCurrentRenderSettings = settings;
        const auto &renderSettings = settings->getRenderSettings();
        setFrameAction(renderSettings.fMinFrame);

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
    mCurrentPreviewFrame = frame;
}

void RenderHandler::cacheAroundFrame(const int frame)
{
    if(!eSettings::instance().fPreviewCache) return;

    setCurrentScene(mDocument.fActiveScene);
    if(!mCurrentScene) return;

    const auto state = mPreviewState;
    if(state == PreviewState::playing || state == PreviewState::rendering) {
        return;
    }

    if(state == PreviewState::paused) {
        ensurePreviewWindowQueued(frame);
        return;
    }

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

    const auto &cacheHandler = mCurrentScene->getSceneFramesHandler();
    const int firstMissing = cacheHandler.firstEmptyFrameAtOrAfter(minFrame);
    mBackgroundCacheVisibleFrame = frame;

    if(firstMissing > maxFrame) {
        mCurrentScene->setMinFrameUseRange(minFrame);
        mCurrentScene->setMaxFrameUseRange(maxFrame);
        if(mCurrentSoundComposition) {
            mCurrentSoundComposition->setMinFrameUseRange(minFrame);
            mCurrentSoundComposition->setMaxFrameUseRange(maxFrame);
        }
        stopBackgroundCaching();
        return;
    }

    if(!mBackgroundCaching) {
        const auto nextFrameFunc = [this]() {
            nextPreviewRenderFrame();
        };
        TaskScheduler::sSetTaskUnderflowFunc(nextFrameFunc);
        TaskScheduler::sSetAllTasksFinishedFunc(nextFrameFunc);
        mBackgroundCaching = true;
        setRenderingPreview(true);
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
    mDocument.actionFinished();
}

void RenderHandler::setCurrentScene(Canvas * const scene) {
    mCurrentScene = scene;
    mCurrentSoundComposition = scene ? scene->getSoundComposition() : nullptr;
}

void RenderHandler::nextCurrentRenderFrame() {
    auto& cacheHandler = mCurrentScene->getSceneFramesHandler();
    int newCurrentRenderFrame = cacheHandler.
            firstEmptyFrameAtOrAfter(mCurrentRenderFrame + 1);
    const bool allDone = newCurrentRenderFrame > mMaxRenderFrame;
    newCurrentRenderFrame = qMin(mMaxRenderFrame, newCurrentRenderFrame);
    const FrameRange newSoundRange = {mCurrentRenderFrame, newCurrentRenderFrame};
    mCurrentSoundComposition->scheduleFrameRange(newSoundRange);
    mCurrentSoundComposition->setMaxFrameUseRange(newCurrentRenderFrame);
    mCurrentScene->setMaxFrameUseRange(newCurrentRenderFrame);

    mCurrentRenderFrame = newCurrentRenderFrame;
    mCurrRenderRange.fMax = mCurrentRenderFrame;
    if(allDone) Document::sInstance->actionFinished();
    else {
        const bool keepVisiblePreviewFrame =
                mPreviewState == PreviewState::playing ||
                mPreviewState == PreviewState::paused ||
                mBackgroundCaching;
        if(keepVisiblePreviewFrame) {
            const int visibleFrame =
                    mPreviewState == PreviewState::playing ||
                    mPreviewState == PreviewState::paused ?
                        mCurrentPreviewFrame :
                        mBackgroundCacheVisibleFrame;
            const QSignalBlocker blocker(mCurrentScene);
            setFrameAction(mCurrentRenderFrame);
            const auto visibleCont =
                    mCurrentScene->getSceneFramesHandler().
                    sharedAtFrame<SceneFrameContainer>(visibleFrame);
            mCurrentScene->anim_setAbsFrame(visibleFrame);
            if(visibleCont) {
                mCurrentScene->setSceneFrame(visibleCont);
            }
            emit mCurrentScene->requestUpdate();
        } else {
            setFrameAction(mCurrentRenderFrame);
        }
    }
}

void RenderHandler::setPreviewState(const PreviewState state)
{
    if (mPreviewState == state) { return; }
    const bool keepBackgroundPreviewRendering =
            (state == PreviewState::playing ||
             state == PreviewState::paused) &&
            eSettings::instance().fPreviewCache &&
            mRenderingPreview;
    setRenderingPreview(state == PreviewState::rendering ||
                        keepBackgroundPreviewRendering);
    setPreviewing(state == PreviewState::playing);
    mPreviewState = state;
}

void RenderHandler::renderPreview() {
    setCurrentScene(mDocument.fActiveScene);
    if(!mCurrentScene) return;
    const auto nextFrameFunc = [this]() {
        nextPreviewRenderFrame();
    };
    TaskScheduler::sSetTaskUnderflowFunc(nextFrameFunc);
    TaskScheduler::sSetAllTasksFinishedFunc(nextFrameFunc);

    mSavedCurrentFrame = mCurrentScene->getCurrentFrame();

    const auto fIn = mCurrentScene->getFrameIn();
    const auto fOut = mCurrentScene->getFrameOut();

    mMinRenderFrame = mLoop ? (fIn.enabled? fIn.frame : mCurrentScene->getMinFrame()) - 1:
                          (fIn.enabled ? fIn.frame : mSavedCurrentFrame);

    mMaxRenderFrame = fOut.enabled ? fOut.frame : mCurrentScene->getMaxFrame();

    mCurrentRenderFrame = mMinRenderFrame;
    mCurrRenderRange = {mCurrentRenderFrame, mCurrentRenderFrame};
    mCurrentScene->setMinFrameUseRange(mCurrentRenderFrame);
    mCurrentSoundComposition->setMinFrameUseRange(mCurrentRenderFrame);

    setPreviewState(PreviewState::rendering);

    emit previewBeingRendered();

    if(TaskScheduler::sAllQuedCpuTasksFinished()) {
        nextPreviewRenderFrame();
    }
}

void RenderHandler::interruptPreview() {
    if(mRenderingPreview) interruptPreviewRendering();
    else if(mPreviewState == PreviewState::playing ||
            mPreviewState == PreviewState::paused) {
        stopPreview();
    }
}

void RenderHandler::outOfMemory() {
    if(mRenderingPreview) {
        if(mBackgroundCaching) {
            stopBackgroundCaching();
            return;
        }
        playPreview();
    }
}

void RenderHandler::setRenderingPreview(const bool rendering) {
    mRenderingPreview = rendering;
    if(mCurrentScene) mCurrentScene->setRenderingPreview(rendering);
    TaskScheduler::instance()->setAlwaysQue(rendering);
}

void RenderHandler::setPreviewing(const bool previewing) {
    mPreviewing = previewing;
    if(mCurrentScene) mCurrentScene->setPreviewing(previewing);
}

void RenderHandler::interruptPreviewRendering() {
    TaskScheduler::sClearAllFinishedFuncs();
    if(mBackgroundCaching) {
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
        mCurrentScene->clearUseRange();
        int targetFrame = mSavedCurrentFrame;
        if (mPreviewState == PreviewState::playing ||
            mPreviewState == PreviewState::paused) {
            targetFrame = qBound(mMinPreviewFrame,
                                 mCurrentPreviewFrame,
                                 mMaxPreviewFrame);
        }
        setFrameAction(targetFrame);
        mCurrentScene->setSceneFrame(targetFrame);
        emit mCurrentScene->currentFrameChanged(targetFrame);
        emit mCurrentScene->requestUpdate();
    }

    mPreviewFPSTimer->stop();
    stopAudio();
    emit previewFinished();
    setPreviewState(PreviewState::stopped);
}

void RenderHandler::pausePreview() {
    if(mPreviewState == PreviewState::playing) {
        stopAudio();
        mPreviewFPSTimer->stop();
        emit previewPaused();
        setPreviewState(PreviewState::paused);
    }
}

void RenderHandler::resumePreview() {
    if(mPreviewState == PreviewState::paused) {
        startAudio();
        mPreviewFPSTimer->start();
        emit previewBeingPlayed();
        setPreviewState(PreviewState::playing);
    }
}

void RenderHandler::playPreviewAfterAllTasksCompleted() {
    if(mRenderingPreview) {
        TaskScheduler::sSetTaskUnderflowFunc(nullptr);
        Document::sInstance->actionFinished();
        if(TaskScheduler::sAllTasksFinished()) {
            playPreview();
        } else {
            TaskScheduler::sSetAllTasksFinishedFunc([this]() {
                playPreview();
            });
        }
    }
}

void RenderHandler::playPreview() {
    if(!mCurrentScene) {
        setCurrentScene(mDocument.fActiveScene);
    }
    if(!mCurrentScene) return;
    if(mBackgroundCaching) {
        stopBackgroundCaching();
    }
    if(mPreviewState == PreviewState::stopped && !mRenderingPreview) {
        renderPreview();
    }
    //setFrameAction(mSavedCurrentFrame);
    if(!eSettings::instance().fPreviewCache) {
        TaskScheduler::sClearAllFinishedFuncs();
    } else {
        const auto nextFrameFunc = [this]() {
            nextPreviewRenderFrame();
        };
        TaskScheduler::sSetTaskUnderflowFunc(nextFrameFunc);
        TaskScheduler::sSetAllTasksFinishedFunc(nextFrameFunc);
    }
    const auto fIn = mCurrentScene->getFrameIn();
    const auto fOut = mCurrentScene->getFrameOut();
    const int minPreviewFrame = fIn.enabled ?
                (fIn.frame < mSavedCurrentFrame && mSavedCurrentFrame < mMaxRenderFrame ?
                     mSavedCurrentFrame : fIn.frame) :
                mSavedCurrentFrame;
    const int maxPreviewFrame = mMaxRenderFrame;
    if(minPreviewFrame > maxPreviewFrame) return;
    mMinPreviewFrame = mLoop ? (fIn.enabled? fIn.frame : mCurrentScene->getMinFrame()) : (fIn.enabled? (fIn.frame < minPreviewFrame && minPreviewFrame < mCurrentRenderFrame ? minPreviewFrame : fIn.frame) : minPreviewFrame);
    mMaxPreviewFrame = fOut.enabled ? fOut.frame : maxPreviewFrame;
    mCurrentPreviewFrame = minPreviewFrame;
    mCurrentScene->setSceneFrame(mCurrentPreviewFrame);
    ensurePreviewWindowQueued(mCurrentPreviewFrame);

    setPreviewState(PreviewState::playing);

    startAudio();

    mPreviewNominalIntervalMs = qMax(1, qRound(1000/mCurrentScene->getFps()));
    mPreviewWaitingForCache = false;
    mPreviewAudioPausedForCache = false;
    mPreviewAudioNeedsRestart = false;
    mPreviewRenderCursorRetargeted = false;
    mPreviewFPSTimer->setInterval(mPreviewNominalIntervalMs);
    mPreviewFPSTimer->start();
    emit previewBeingPlayed();
    emit mCurrentScene->requestUpdate();
}

void RenderHandler::nextPreviewRenderFrame() {
    if(!mRenderingPreview) return;
    if(mCurrentRenderFrame >= mMaxRenderFrame) {
        if(mBackgroundCaching) {
            stopBackgroundCaching();
        } else {
            playPreviewAfterAllTasksCompleted();
        }
    } else {
        nextCurrentRenderFrame();
        if(TaskScheduler::sAllTasksFinished()) {
            nextPreviewRenderFrame();
        }
    }
}

void RenderHandler::nextPreviewFrame() {
    if(!mCurrentScene) return;
    const int nextFrame = mCurrentPreviewFrame + 1;
    if(nextFrame > mMaxPreviewFrame) {
        if(mLoop) {
            const int savedFrame = mCurrentPreviewFrame;
            mCurrentPreviewFrame = mMinPreviewFrame - 1;
            nextPreviewFrame();
            if (mCurrentPreviewFrame == mMinPreviewFrame - 1) {
                mCurrentPreviewFrame = savedFrame;
            } else {
                stopAudio();
                startAudio();
            }
        } else stopPreview();
    } else {
        ensurePreviewWindowQueued(nextFrame);
        const auto cachedFrame =
                mCurrentScene->getSceneFramesHandler().atFrame<SceneFrameContainer>(nextFrame);
        if(!cachedFrame) {
            mPreviewWaitingForCache = true;
            ensurePreviewWindowQueued(nextFrame);
            mPreviewFPSTimer->setInterval(qMax(mPreviewNominalIntervalMs,
                                               mPreviewNominalIntervalMs*2));
            if(!mPreviewAudioPausedForCache) {
                stopAudio();
                mPreviewAudioPausedForCache = true;
                mPreviewAudioNeedsRestart = true;
            }
            emit mCurrentScene->requestUpdate();
            return;
        }
        if(mPreviewWaitingForCache) {
            mPreviewWaitingForCache = false;
            mPreviewFPSTimer->setInterval(mPreviewNominalIntervalMs);
        }
        if(mPreviewAudioPausedForCache || mPreviewAudioNeedsRestart) {
            startAudio();
            mPreviewAudioPausedForCache = false;
            mPreviewAudioNeedsRestart = false;
        }
        mCurrentPreviewFrame = nextFrame;
        mCurrentScene->setSceneFrame(mCurrentPreviewFrame);
        if(!mLoop) mCurrentScene->setMinFrameUseRange(mCurrentPreviewFrame);
        emit mCurrentScene->currentFrameChanged(mCurrentPreviewFrame);
    }
    emit mCurrentScene->requestUpdate();
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
    VideoEncoder::sFinishEncoding();
}

void RenderHandler::nextSaveOutputFrame() {
    const auto& sCacheHandler = mCurrentSoundComposition->getCacheHandler();
    const qreal fps = mCurrentScene->getFps();
    const int sampleRate = eSoundSettings::sSampleRate();
    while(mCurrentEncodeSoundSecond <= mMaxSoundSec) {
        const auto cont = sCacheHandler.atFrame(mCurrentEncodeSoundSecond);
        if(!cont) break;
        const auto sCont = cont->ref<SoundCacheContainer>();
        const auto samples = sCont->getSamples();
        if(mCurrentEncodeSoundSecond == mFirstEncodeSoundSecond) {
            const int minSample = qRound(mMinRenderFrame*sampleRate/fps);
            const int max = samples->fSampleRange.fMax;
            VideoEncoder::sAddCacheContainerToEncoder(
                        samples->mid({minSample, max}));
        } else {
            VideoEncoder::sAddCacheContainerToEncoder(
                        enve::make_shared<Samples>(samples));
        }
        mCurrentEncodeSoundSecond++;
    }
    if(mCurrentEncodeSoundSecond > mMaxSoundSec) VideoEncoder::sAllAudioProvided();

    const auto& cacheHandler = mCurrentScene->getSceneFramesHandler();
    while(mCurrentEncodeFrame <= mMaxRenderFrame) {
        const auto cont = cacheHandler.atFrame(mCurrentEncodeFrame);
        if(!cont) break;
        VideoEncoder::sAddCacheContainerToEncoder(cont->ref<SceneFrameContainer>());
        mCurrentEncodeFrame = cont->getRangeMax() + 1;
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
        mCurrentRenderSettings->setCurrentRenderFrame(mCurrentRenderFrame);
        nextCurrentRenderFrame();
        if(TaskScheduler::sAllTasksFinished()) {
            nextSaveOutputFrame();
        }
    }
}

void RenderHandler::startAudio() {
    mAudioHandler.startAudio();
    if(mCurrentSoundComposition)
        mCurrentSoundComposition->start(mCurrentPreviewFrame);
    audioPushTimerExpired();
}

void RenderHandler::stopAudio() {
    mAudioHandler.stopAudio();
    if(mCurrentSoundComposition) mCurrentSoundComposition->stop();
    mPreviewAudioPausedForCache = false;
}

void RenderHandler::audioPushTimerExpired() {
    if(!mCurrentSoundComposition) return;
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
    const int cacheSpanForward = qMax(24, qRound(mCurrentScene->getFps()*1.5));
    const int cacheSpanBackward = qMax(6, qRound(mCurrentScene->getFps()*0.25));
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
    const auto &cacheHandler = mCurrentScene->getSceneFramesHandler();
    const int firstMissing = cacheHandler.firstEmptyFrameAtOrAfter(anchorFrame);
    if(firstMissing <= maxFrame) {
        maxFrame = qMax(firstMissing + cacheSpanForward/2, anchorFrame);
        maxFrame = qMin(maxFrame, mMaxRenderFrame);
        if(fOut.enabled) {
            maxFrame = qMin(maxFrame, fOut.frame);
        }
    }

    if(mRenderingPreview && firstMissing <= maxFrame) {
        const bool renderCursorOutsideWindow =
                mCurrentRenderFrame < minFrame || mCurrentRenderFrame > maxFrame;
        const bool renderCursorAheadOfNeed = mCurrentRenderFrame > firstMissing;
        if(renderCursorOutsideWindow || renderCursorAheadOfNeed) {
            mCurrentRenderFrame = qMax(mMinRenderFrame, firstMissing - 1);
            mCurrRenderRange.fMax = mCurrentRenderFrame;
            mPreviewRenderCursorRetargeted = true;
        }
    }

    mCurrentScene->setMinFrameUseRange(minFrame);
    mCurrentScene->setMaxFrameUseRange(maxFrame);
    if(mCurrentSoundComposition) {
        mCurrentSoundComposition->setMinFrameUseRange(minFrame);
        mCurrentSoundComposition->setMaxFrameUseRange(maxFrame);
        mCurrentSoundComposition->scheduleFrameRange({minFrame, maxFrame});
    }
    mDocument.updateScenes();

    if(mPreviewRenderCursorRetargeted && TaskScheduler::sAllTasksFinished()) {
        mPreviewRenderCursorRetargeted = false;
        nextPreviewRenderFrame();
    }
}

void RenderHandler::stopBackgroundCaching()
{
    if(!mBackgroundCaching) return;
    mBackgroundCaching = false;
    mPreviewRenderCursorRetargeted = false;
    TaskScheduler::sSetTaskUnderflowFunc(nullptr);
    TaskScheduler::sSetAllTasksFinishedFunc(nullptr);
    setRenderingPreview(false);
}
