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

#ifndef RENDERHANDLER_H
#define RENDERHANDLER_H
#include "framerange.h"
#include "Sound/audiohandler.h"
#include "smartPointers/ememory.h"
#include "CacheHandlers/usepointer.h"
#include "CacheHandlers/cachecontainer.h"
#include "conncontextptr.h"
#include <QElapsedTimer>
#include <QReadWriteLock>

class Canvas;
class RenderInstanceSettings;
class SoundComposition;
class Document;
class VideoEncoder;
class MemoryHandler;
class SceneFrameContainer;

enum class PreviewState {
    stopped, playing, paused
};

class RenderHandler : public QObject {
    Q_OBJECT
public:
    RenderHandler(Document &document,
                  AudioHandler &audioHandler,
                  VideoEncoder &videoEncoder,
                  MemoryHandler &memoryHandler);

    void interruptPreview();
    void outOfMemory();
    void interruptPreviewRendering();
    void interruptOutputRendering();

    void playPreview();
    void stopPreview();
    void pausePreview();
    void resumePreview();
    void renderFromSettings(RenderInstanceSettings * const settings);

    void setPreviewFrame(const int &frame);
    void cacheAroundFrame(const int frame);

    void setLoop(const bool loop);

    PreviewState currentPreviewState() const
    { return previewStateValue(); }

    static RenderHandler* sInstance;
signals:
    void previewPaused();
    void previewBeingPlayed();
    void previewFinished();
private:
    struct PreviewFrameState {
        int fCurrentFrame;
        int fMinFrame;
        int fMaxFrame;
    };

    struct PreviewTimingState {
        int fNominalIntervalMs;
        int fTickIntervalMs;
        qreal fFrameAccumulator;
        qreal fPlaybackRate;
        bool fWaitingForCache;
        bool fAudioPausedForCache;
        bool fAudioNeedsRestart;
        bool fAudioActive;
        bool fRenderCursorRetargeted;
        bool fBackgroundCaching;
        int fBackgroundCacheVisibleFrame;
        qreal fBaseResolution;
    };

    bool sceneFrameMatchesCurrentPreview(const SceneFrameContainer *cont) const;
    int firstMissingUsablePreviewFrameAtOrAfter(int frame, int maxFrame) const;
    int firstUnreadyPreviewFrameAtOrAfter(int frame, int maxFrame) const;
    bool previewFrameReady(int frame) const;
    int previewSteadyBufferFrames() const;
    int previewRequiredBufferFrames(int anchorFrame, int desiredFrames) const;
    int previewBufferedAheadFrames(int anchorFrame, int maxFrames) const;
    bool previewHasBufferedAhead(int anchorFrame, int targetFrames) const;
    qreal previewPlaybackRateForBufferedFrames(int bufferedFrames,
                                               int targetFrames) const;
    void updatePreviewPlaybackRate(int anchorFrame);
    void syncPreviewUseRange(const FrameRange &baseRange);
    void restorePreviewResolution();
    void trimPreviewCaches(const FrameRange &activeRange);
    int warmPreviewFramesInMemory(const FrameRange &range, int maxFrames);
    void activateInteractivePreviewCaching();
    void suspendInteractivePreviewCaching();
    void invalidateSceneFrameIfStale(int frame);
    void ensureSceneFrameQueuedForCurrentRenderFrame();
    void handleActiveSceneChanged(Canvas *scene);
    void handleCurrentSceneDestroyed(Canvas *destroyedScene);
    void setScenePreviewDisplayFrame(int frame, bool emitFrameChanged);
    void showScenePreviewFrame(int frame, bool emitFrameChanged);
    void restoreSceneAnimationFrame(int frame);

    void setFrameAction(const int frame);
    void setInternalFrameAction(const int frame);
    void setCurrentScene(Canvas * const scene);

    void finishEncoding();
    void queueFinishEncoding();
    void queueNextSaveOutputFrame();
    void nextSaveOutputFrame();
    void nextPreviewRenderFrame();
    void nextPreviewFrame();
    void nextCurrentRenderFrame();
    void ensurePreviewWindowQueued(int anchorFrame);
    bool applyPreviewWindowRange(const FrameRange &range,
                                 bool scheduleAudio);
    void stopBackgroundCaching();

    void setPreviewState(const PreviewState state);
    void setRenderingPreview(const bool rendering);
    void setPreviewing(const bool previewing);
    PreviewState previewStateValue() const;
    int currentPreviewFrameValue() const;
    PreviewFrameState previewFrameStateValue() const;
    bool previewingValue() const;
    bool renderingPreviewValue() const;
    void setCurrentPreviewFrameValue(int frame);
    void setPreviewFrameState(int minFrame, int maxFrame, int currentFrame);
    PreviewTimingState previewTimingStateValue() const;
    void setPreviewTimingState(const PreviewTimingState &state);
    FrameRange queuedPreviewWindowValue(bool *valid = nullptr) const;
    void setQueuedPreviewWindowValue(const FrameRange &range, bool valid);
    FrameRange pinnedPreviewWindowValue(bool *valid = nullptr) const;
    void setPinnedPreviewWindowValue(const FrameRange &range, bool valid);
    FrameRange previewPlayedRangeValue() const;
    void setPreviewPlayedRangeValue(const FrameRange &range);
    void appendPreviewPlayedFrameValue(int frame);
    bool backgroundCachingValue() const;
    bool previewRenderCursorRetargetedValue() const;
    void setPreviewRenderCursorRetargetedValue(bool retargeted);

    void startAudio();
    void audioPushTimerExpired();
    void stopAudio();

    Document& mDocument;

    bool mLoop = false;

    // AUDIO
    AudioHandler& mAudioHandler;
    VideoEncoder& mVideoEncoder;
    qptr<SoundComposition> mCurrentSoundComposition;
    // AUDIO

    ConnContextQPtr<Canvas> mCurrentScene;
    QTimer *mPreviewFPSTimer = nullptr;
    RenderInstanceSettings *mCurrentRenderSettings = nullptr;
    bool mOutputAdvanceQueued = false;
    bool mFinishEncodingQueued = false;

    int mCurrentPreviewFrame;
    int mMaxPreviewFrame;
    int mMinPreviewFrame;

    PreviewState mPreviewState = PreviewState::stopped;
    //! @brief true if preview is currently playing
    bool mPreviewing = false;
    //! @brief true if currently preview is being rendered
    bool mRenderingPreview = false;

    int mCurrentEncodeFrame;
    int mCurrentEncodeSoundSecond;
    int mMaxSoundSec;
    int mFirstEncodeSoundSecond;

    FrameRange mCurrRenderRange;
    int mCurrentRenderFrame;
    int mMinRenderFrame = 0;
    int mMaxRenderFrame = 0;

    int mSavedCurrentFrame = 0;
    qreal mSavedResolutionFraction = 100;
    int mPreviewNominalIntervalMs = 0;
    int mPreviewTickIntervalMs = 16;
    QElapsedTimer mPreviewTickClock;
    qreal mPreviewFrameAccumulator = 0;
    qreal mPreviewPlaybackRate = 1;
    bool mPreviewWaitingForCache = false;
    bool mPreviewAudioPausedForCache = false;
    bool mPreviewAudioNeedsRestart = false;
    bool mPreviewAudioActive = false;
    bool mPreviewRenderCursorRetargeted = false;
    bool mBackgroundCaching = false;
    int mBackgroundCacheVisibleFrame = 0;
    FrameRange mQueuedPreviewWindow = {0, -1};
    bool mQueuedPreviewWindowValid = false;
    FrameRange mPreviewPlayedRange = {0, -1};
    FrameRange mPinnedPreviewWindow = {0, -1};
    bool mPinnedPreviewWindowValid = false;
    qreal mPreviewBaseResolution = 1;
    mutable QReadWriteLock mPreviewStateLock;
};

#endif // RENDERHANDLER_H
