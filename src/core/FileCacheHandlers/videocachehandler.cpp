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

#include "videocachehandler.h"
#include "Boxes/boxrendercontainer.h"
#include "Boxes/videobox.h"
#include "CacheHandlers/imagecachecontainer.h"

#include "GUI/edialogs.h"
#include "filesourcescache.h"

#include "videoframeloader.h"

namespace {

constexpr int kVideoSequentialPrefetchFrames = 2;
constexpr int kVideoMaxPendingFrameLoads = 6;

template<typename T>
qsptr<T> getOrCreateDataHandler(const QString& filePath,
                                const bool reloadExisting = true) {
    if(const auto existing = FileDataCacheHandler::sGetDataHandler<T>(filePath)) {
        const auto shared = existing->template ref<T>();
        if(reloadExisting) {
            shared->reload();
        }
        return shared;
    }
    return FileDataCacheHandler::sCreateDataHandler<T>(filePath);
}

}

class VideoSourceInfoTask : public eHddTask {
    e_OBJECT
public:
    VideoSourceInfoTask(VideoDataHandler * const target,
                        const QString &filePath,
                        const int requestId) :
        mTarget(target),
        mFilePath(filePath),
        mRequestId(requestId) {}

    void process() override {
        try {
            mInfo = VideoStreamsData::sInspect(mFilePath);
        } catch(...) {
            // Keep the handler responsive even if the source is missing
            // or currently unreadable. The UI can then degrade gracefully.
            mInfo = VideoStreamsData::SourceInfo{};
        }
    }

    void afterProcessing() override {
        if(mTarget) {
            mTarget->applySourceInfo(mInfo, mFilePath, mRequestId);
        }
    }

private:
    const qptr<VideoDataHandler> mTarget;
    const QString mFilePath;
    const int mRequestId = 0;
    VideoStreamsData::SourceInfo mInfo;
};

VideoFrameHandler::VideoFrameHandler(VideoDataHandler * const cacheHandler) :
    mDataHandler(cacheHandler) {
    if(mDataHandler) {
        mDataHandler->registerFrameHandler(this);
    }
}

VideoFrameHandler::~VideoFrameHandler() {
    if(mDataHandler) {
        mDataHandler->unregisterFrameHandler(this);
    }
}

ImageCacheContainer* VideoFrameHandler::getFrameAtFrame(const int relFrame) {
    return mDataHandler->getFrameAtFrame(relFrame);
}

ImageCacheContainer* VideoFrameHandler::getFrameAtOrBeforeFrame(const int relFrame) {
    return mDataHandler->getFrameAtOrBeforeFrame(relFrame);
}

void VideoFrameHandler::frameLoaderFinished(const int frame,
                                            const sk_sp<SkImage>& image) {
    mDataHandler->frameLoaderFinished(frame, image);
    removeFrameLoader(frame);
}

void VideoFrameHandler::frameLoaderCanceled(const int frameId) {
    removeFrameLoader(frameId);
}

void VideoFrameHandler::frameLoaderFailed(const int frameId) {
    removeFrameLoader(frameId);
}

VideoDataHandler *VideoFrameHandler::getDataHandler() const {
    return mDataHandler;
}

const HddCachableCacheHandler &VideoFrameHandler::getCacheHandler() const {
    return mDataHandler->getCacheHandler();
}

VideoFrameLoader *VideoFrameHandler::getFrameLoader(const int frame) {
    return mDataHandler->getFrameLoader(frame);
}

VideoFrameLoader *VideoFrameHandler::addFrameLoader(
        const int frameId,
        const VideoFrameAccessMode accessMode) {
    if(!openVideoStream()) {
        return nullptr;
    }
    const auto loader = enve::make_shared<VideoFrameLoader>(
                    this, mVideoStreamsData, frameId, accessMode);
    mDataHandler->addFrameLoader(frameId, loader);
    for(const auto& nFrame : mNeededFrames) {
        const auto nLoader = getFrameLoader(nFrame);
        if(nFrame < frameId) nLoader->addDependent(loader.get());
        else loader->addDependent(nLoader);
    }
    mNeededFrames.insert(frameId);

    return loader.get();
}

VideoFrameLoader *VideoFrameHandler::addFrameConverter(
        const int frameId,  AVFrame * const frame) {
    if(!openVideoStream()) {
        return nullptr;
    }
    const auto loader = enve::make_shared<VideoFrameLoader>(
                    this, mVideoStreamsData, frameId, frame);
    mDataHandler->addFrameLoader(frameId, loader);
    return loader.get();
}

void VideoFrameHandler::removeFrameLoader(const int frame) {
    mDataHandler->removeFrameLoader(frame);
    mNeededFrames.erase(frame);
}

bool VideoFrameHandler::openVideoStream()
{
    if(mVideoStreamsData && mVideoStreamsData->fOpened) {
        return true;
    }
    const auto filePath = mDataHandler->getFilePath();
    try {
        mVideoStreamsData = VideoStreamsData::sOpen(filePath);
        return mVideoStreamsData && mVideoStreamsData->fOpened;
    } catch(const std::exception& e) {
        mVideoStreamsData.reset();
        gPrintExceptionCritical(e);
    }
    return false;
}

eTask* VideoFrameHandler::scheduleFrameLoad(const int frame) {
    const int previousFrame = mLastRequestedFrame;
    const bool sequentialForward =
            previousFrame >= 0 &&
            frame >= previousFrame &&
            frame - previousFrame <= 1;
    const auto accessMode = sequentialForward ?
                VideoFrameAccessMode::sequential :
                VideoFrameAccessMode::seek;
    mLastRequestedFrame = frame;
    if(!sequentialForward) {
        mLastPrefetchAnchorFrame = frame - 1;
    }

    const auto task = scheduleFrameLoadInternal(frame, true, accessMode);
    if(sequentialForward) {
        scheduleSequentialPrefetch(frame);
    }
    return task;
}

eTask *VideoFrameHandler::scheduleFrameLoadInternal(
        const int frame,
        const bool throwOnOutOfRange,
        const VideoFrameAccessMode accessMode) {
    Q_UNUSED(throwOnOutOfRange)
    if(frame < 0) {
        return nullptr;
    }

    const int frameCount = getFrameCount();
    if(frameCount <= 0) {
        mDataHandler->ensureSourceInfo();
        return nullptr;
    }

    if(frame >= frameCount) {
        return nullptr;
    }

    const auto currLoader = getFrameLoader(frame);
    if(currLoader) return currLoader;
    if(mDataHandler->getFrameAtFrame(frame)) return nullptr;
    const auto loadTask = mDataHandler->scheduleFrameHddCacheLoad(frame);
    if(loadTask) return loadTask;
    const auto loader = addFrameLoader(frame, accessMode);
    if(!loader) return nullptr;
    loader->queTask();
    return loader;
}

void VideoFrameHandler::scheduleSequentialPrefetch(const int frame) {
    if(frame < 0) {
        return;
    }

    const int frameCount = getFrameCount();

    if(frameCount <= 0 || mDataHandler->sourceInfoPending()) {
        return;
    }
    if(frame >= frameCount || frame <= mLastPrefetchAnchorFrame) {
        return;
    }

    mLastPrefetchAnchorFrame = frame;
    int scheduled = 0;
    for(int nextFrame = frame + 1;
        nextFrame < frameCount &&
        scheduled < kVideoSequentialPrefetchFrames &&
        mDataHandler->pendingFrameLoadCount() < kVideoMaxPendingFrameLoads;
        nextFrame++) {
        if(scheduleFrameLoadInternal(nextFrame, false,
                                     VideoFrameAccessMode::sequential)) {
            scheduled++;
        }
    }
}

int VideoFrameHandler::getFrameCount() const {
    return mDataHandler->getFrameCount();
}

qreal VideoFrameHandler::getSourceFps() const {
    return mDataHandler->getFps();
}

void VideoFrameHandler::reload() {
    mVideoStreamsData.reset();
    mNeededFrames.clear();
    mLastRequestedFrame = -1;
    mLastPrefetchAnchorFrame = -1;
    mDataHandler->reload();
}

void VideoFrameHandler::afterSourceChanged() {
    mVideoStreamsData.reset();
    mNeededFrames.clear();
    mLastRequestedFrame = -1;
    mLastPrefetchAnchorFrame = -1;
}

void VideoDataHandler::clearCache() {
    mFramesCache.clear();
    mFramesBeingLoaded.clear();
    const auto frameLoaders = mFrameLoaders;
    for(const auto& loader : frameLoaders)
        loader->cancel();
    mFrameLoaders.clear();
    if(mSourceInfoTask && mSourceInfoTask->isActive()) {
        mSourceInfoTask->cancel();
    }
}

void VideoFileHandler::replace() {
    const QString importPath = eDialogs::openFile(
                "Replace Video Source " + path(), path(),
                "Video Files (" + FileExtensions::videoFilters() + ")");
    if(!importPath.isEmpty()) {
        const QFile file(importPath);
        if(!file.exists()) return;
        if(hasVideoExt(importPath)) {
            try {
                setPath(importPath);
            } catch(const std::exception& e) {
                gPrintExceptionCritical(e);
            }
        }
    }
}

void VideoDataHandler::afterSourceChanged() {
    if(isFileMissing()) {
        if(mSourceInfoTask && mSourceInfoTask->isActive()) {
            mSourceInfoTask->cancel();
        }
        setSourceInfoPending(false);
        setFrameCount(0);
        setFps(0);
        setDim(QSize());
        setHasAudio(false);
        emit frameCountUpdated(mFrameCount);
        emit sourceInfoUpdated();
    } else {
        queueSourceInfoRefresh();
    }
    for(const auto& handler : mFrameHandlers) {
        handler->afterSourceChanged();
    }
}

const HddCachableCacheHandler &VideoDataHandler::getCacheHandler() const {
    return mFramesCache;
}

void VideoDataHandler::addFrameLoader(const int frameId,
                                      const stdsptr<VideoFrameLoader> &loader) {
    mFramesBeingLoaded << frameId;
    mFrameLoaders << loader;
}

VideoFrameLoader *VideoDataHandler::getFrameLoader(const int frame) const {
    const int id = mFramesBeingLoaded.indexOf(frame);
    if(id >= 0) return mFrameLoaders.at(id).get();
    return nullptr;
}

void VideoDataHandler::removeFrameLoader(const int frame) {
    const int id = mFramesBeingLoaded.indexOf(frame);
    if(id < 0 || id >= mFramesBeingLoaded.count()) return;
    mFramesBeingLoaded.removeAt(id);
    mFrameLoaders.removeAt(id);
}

void VideoDataHandler::frameLoaderFinished(const int frame,
                                           const sk_sp<SkImage> &image) {
    if(image) {
        mFramesCache.add(enve::make_shared<ImageCacheContainer>(
                             image, FrameRange{frame, frame}, &mFramesCache));
    }
}

eTask *VideoDataHandler::scheduleFrameHddCacheLoad(const int frame) {
    const auto contAtFrame = mFramesCache.atFrame<ImageCacheContainer>(frame);
    if(contAtFrame) return contAtFrame->scheduleLoadFromTmpFile();
    return nullptr;
}

ImageCacheContainer* VideoDataHandler::getFrameAtFrame(const int relFrame) const {
    return mFramesCache.atFrame<ImageCacheContainer>(relFrame);
}

ImageCacheContainer* VideoDataHandler::getFrameAtOrBeforeFrame(const int relFrame) const {
    return mFramesCache.atOrBeforeFrame<ImageCacheContainer>(relFrame);
}

int VideoDataHandler::getFrameCount() const { return mFrameCount; }

void VideoDataHandler::setFrameCount(const int count) { mFrameCount = count; }

qreal VideoDataHandler::getFps() const
{
    return mFrameFps;
}

void VideoDataHandler::setFps(const qreal fps)
{
    mFrameFps = fps;
}

const QSize VideoDataHandler::getDim()
{
    return QSize(mFrameWidth, mFrameHeight);
}

void VideoDataHandler::setDim(const QSize dim)
{
    mFrameWidth = dim.width();
    mFrameHeight = dim.height();
}

bool VideoDataHandler::hasAudio() const {
    return mHasAudio;
}

void VideoDataHandler::setHasAudio(const bool hasAudio) {
    mHasAudio = hasAudio;
}

bool VideoDataHandler::sourceInfoPending() const {
    return mSourceInfoPending;
}

void VideoDataHandler::ensureSourceInfo() {
    if(mFileMissing || mSourceInfoPending || mFrameCount > 0) {
        return;
    }
    queueSourceInfoRefresh();
}

int VideoDataHandler::pendingFrameLoadCount() const {
    return mFramesBeingLoaded.count();
}

void VideoDataHandler::queueSourceInfoRefresh() {
    mSourceInfoRequestId++;
    if(mSourceInfoTask && mSourceInfoTask->isActive()) {
        mSourceInfoTask->cancel();
    }
    setSourceInfoPending(true);
    const auto task = enve::make_shared<VideoSourceInfoTask>(
                this, getFilePath(), mSourceInfoRequestId);
    mSourceInfoTask = task;
    task->queTask();
}

void VideoDataHandler::setSourceInfoPending(const bool pending) {
    if(mSourceInfoPending == pending) {
        return;
    }
    mSourceInfoPending = pending;
    emit sourceInfoLoadingChanged(mSourceInfoPending);
}

void VideoDataHandler::applySourceInfo(const VideoStreamsData::SourceInfo &info,
                                       const QString &filePath,
                                       int requestId) {
    if(filePath != getFilePath() || requestId != mSourceInfoRequestId) {
        return;
    }
    setSourceInfoPending(false);
    setFrameCount(info.fFrameCount);
    setFps(info.fFps);
    setDim(QSize(info.fWidth, info.fHeight));
    setHasAudio(info.fHasAudio);
    emit frameCountUpdated(mFrameCount);
    emit sourceInfoUpdated();
}

void VideoDataHandler::registerFrameHandler(VideoFrameHandler * const handler) {
    if(handler && !mFrameHandlers.contains(handler)) {
        mFrameHandlers << handler;
    }
}

void VideoDataHandler::unregisterFrameHandler(VideoFrameHandler * const handler) {
    mFrameHandlers.removeOne(handler);
}

void VideoFileHandler::reload() {
    if(fileMissing()) {
        QObject::disconnect(mSourceInfoConn);
        mDataHandler.reset();
        mSoundHandler.reset();
        return;
    }
    const QString currentPath = this->path();
    mDataHandler = getOrCreateDataHandler<VideoDataHandler>(currentPath);
    QObject::disconnect(mSourceInfoConn);
    mSourceInfoConn = connect(mDataHandler.get(), &VideoDataHandler::sourceInfoUpdated,
                              this, [this]() {
        if(!mDataHandler) {
            return;
        }
        if(mDataHandler->hasAudio()) {
            mSoundHandler = getOrCreateDataHandler<SoundDataHandler>(this->path(), false);
        } else {
            mSoundHandler.reset();
        }
        emit reloaded();
    });
    if(mDataHandler && mDataHandler->hasAudio()) {
        mSoundHandler = getOrCreateDataHandler<SoundDataHandler>(currentPath, false);
    } else {
        mSoundHandler.reset();
    }
}
