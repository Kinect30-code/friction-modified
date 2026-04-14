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

#ifndef VIDEOCACHEHANDLER_H
#define VIDEOCACHEHANDLER_H

#include "animationcachehandler.h"
#include "videostreamsdata.h"
#include "filecachehandler.h"
#include "CacheHandlers/hddcachablecachehandler.h"

#include <set>

class VideoFrameLoader;
class VideoFrameHandler;
class VideoSourceInfoTask;

enum class VideoFrameAccessMode {
    sequential,
    seek
};

class CORE_EXPORT VideoDataHandler : public FileDataCacheHandler {
    Q_OBJECT
    friend class VideoSourceInfoTask;
public:
    VideoDataHandler() {}

    void clearCache();
    void afterSourceChanged();

    const HddCachableCacheHandler& getCacheHandler() const;

    void addFrameLoader(const int frameId, const stdsptr<VideoFrameLoader>& loader);
    VideoFrameLoader * getFrameLoader(const int frame) const;
    void removeFrameLoader(const int frame);
    void frameLoaderFinished(const int frame, const sk_sp<SkImage>& image);
    eTask* scheduleFrameHddCacheLoad(const int frame);
    ImageCacheContainer* getFrameAtFrame(const int relFrame) const;
    ImageCacheContainer* getFrameAtOrBeforeFrame(const int relFrame) const;
    int getFrameCount() const;
    void setFrameCount(const int count);
    qreal getFps() const;
    void setFps(const qreal fps);
    const QSize getDim();
    void setDim(const QSize dim);
    bool hasAudio() const;
    void setHasAudio(const bool hasAudio);
    bool sourceInfoPending() const;
    void registerFrameHandler(VideoFrameHandler* const handler);
    void unregisterFrameHandler(VideoFrameHandler* const handler);
    int pendingFrameLoadCount() const;
signals:
    void frameCountUpdated(int);
    void sourceInfoUpdated();
    void sourceInfoLoadingChanged(bool pending);
private:
    void queueSourceInfoRefresh();
    void setSourceInfoPending(bool pending);
    void applySourceInfo(const VideoStreamsData::SourceInfo &info,
                         const QString &filePath,
                         int requestId);

    int mFrameCount = 0;
    qreal mFrameFps = 0;
    int mFrameWidth = 0;
    int mFrameHeight = 0;
    bool mHasAudio = false;
    bool mSourceInfoPending = false;
    int mSourceInfoRequestId = 0;
    QList<VideoFrameHandler*> mFrameHandlers;
    QList<int> mFramesBeingLoaded;
    QList<stdsptr<VideoFrameLoader>> mFrameLoaders;
    stdsptr<VideoSourceInfoTask> mSourceInfoTask;
    HddCachableCacheHandler mFramesCache;
};

class CORE_EXPORT VideoFrameHandler : public AnimationFrameHandler {
    e_OBJECT
    friend class VideoFrameLoader;
protected:
    VideoFrameHandler(VideoDataHandler* const cacheHandler);
public:
    ~VideoFrameHandler();
    ImageCacheContainer* getFrameAtFrame(const int relFrame) override;
    ImageCacheContainer* getFrameAtOrBeforeFrame(const int relFrame) override;
    eTask *scheduleFrameLoad(const int frame) override;
    int getFrameCount() const override;
    qreal getSourceFps() const override;
    void reload() override;

    void afterSourceChanged();

    void frameLoaderFinished(const int frame, const sk_sp<SkImage>& image);
    void frameLoaderCanceled(const int frameId);
    void frameLoaderFailed(const int frameId);

    VideoDataHandler* getDataHandler() const;
    const HddCachableCacheHandler& getCacheHandler() const;
protected:
    VideoFrameLoader * getFrameLoader(const int frame);
    VideoFrameLoader * addFrameLoader(
            const int frameId,
            VideoFrameAccessMode accessMode = VideoFrameAccessMode::sequential);
    VideoFrameLoader * addFrameConverter(const int frameId, AVFrame * const frame);
    void removeFrameLoader(const int frame);

    void openVideoStream();
    eTask *scheduleFrameLoadInternal(int frame,
                                     bool throwOnOutOfRange,
                                     VideoFrameAccessMode accessMode);
    void scheduleSequentialPrefetch(int frame);
private:
    std::set<int> mNeededFrames;
    int mLastRequestedFrame = -1;
    int mLastPrefetchAnchorFrame = -1;

    VideoDataHandler* const mDataHandler;
    stdsptr<VideoStreamsData> mVideoStreamsData;
};
#include "CacheHandlers/soundcachehandler.h"
class CORE_EXPORT VideoFileHandler : public FileCacheHandler {
    e_OBJECT
protected:
    VideoFileHandler() {}

    void reload() override;
public:
    void replace() override;

    VideoDataHandler* getFrameHandler() const {
        return mDataHandler.get();
    }

    SoundDataHandler* getSoundHandler() const {
        return mSoundHandler.get();
    }
private:
    qsptr<VideoDataHandler> mDataHandler;
    qsptr<SoundDataHandler> mSoundHandler;
    QMetaObject::Connection mSourceInfoConn;
};

#endif // VIDEOCACHEHANDLER_H
