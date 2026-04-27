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

#include "imagedatahandler.h"

#include "skia/skiahelpers.h"

namespace {
    constexpr int kMaxImageCopies = 8;
}

ImageDataHandler::ImageDataHandler() {}

ImageDataHandler::ImageDataHandler(const sk_sp<SkImage>& img) :
    mImage(img) {}

int ImageDataHandler::sImageByteCount(const sk_sp<SkImage>& img) {
    if(!img) return 0;
    SkPixmap pixmap;
    if(img->peekPixels(&pixmap)) {
        return pixmap.width()*pixmap.height()*
               pixmap.info().bytesPerPixel();
    }
    return 0;
}

int ImageDataHandler::clearImageMemory() {
    QMutexLocker locker(&mCopyMutex);
    int bytes = getImageByteCount();
    for(const auto& copy : mImageCopies) {
        bytes += sImageByteCount(copy);
    }
    mImage.reset();
    mImageCopies.clear();
    return bytes;
}

int ImageDataHandler::getImageByteCount() const {
    return sImageByteCount(mImage);
}

void ImageDataHandler::drawImage(SkCanvas * const canvas,
                                     const SkFilterQuality filter) const {
    SkPaint paint;
    paint.setFilterQuality(filter);
    canvas->drawImage(mImage, 0, 0, &paint);
}

const sk_sp<SkImage>& ImageDataHandler::getImage() const {
    return mImage;
}

sk_sp<SkImage> ImageDataHandler::requestImageCopy() {
    QMutexLocker locker(&mCopyMutex);
    if(mImageCopies.isEmpty()) return mImage;
    return mImageCopies.takeLast();
}

void ImageDataHandler::addImageCopy(const sk_sp<SkImage> &img) {
    if(!img) return;
    QMutexLocker locker(&mCopyMutex);
    if(mImageCopies.size() >= kMaxImageCopies) {
        mImageCopies.removeFirst();
    }
    mImageCopies << img;
}

void ImageDataHandler::replaceImage(const sk_sp<SkImage> &img) {
    QMutexLocker locker(&mCopyMutex);
    mImage = img;
    mImageCopies.clear();
}
