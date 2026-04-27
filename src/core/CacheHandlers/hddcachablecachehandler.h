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

#ifndef HddCACHABLECACHEHANDLER_H
#define HddCACHABLECACHEHANDLER_H

#include "hddcachablerangecont.h"
#include "rangemap.h"
#include "usepointer.h"
#include "usedrange.h"

#include <QPainter>

class CORE_EXPORT HddCachableCacheHandler {
    friend class UsedRange;
public:
    typedef HddCachableRangeCont Cont;

    HddCachableCacheHandler() : mUsedRange(this) {}

    int cacheGeneration() const { return mCacheGeneration; }

    void invalidateAll() {
        mCacheGeneration++;
    }

    void drawCacheOnTimeline(QPainter * const p,
                             const QRectF &drawRect,
                             const int startFrame,
                             const int endFrame,
                             const qreal unit = 1,
                             const int maxX = INT_MAX/2) const;

    int firstEmptyFrameAtOrAfter(const int frame) const {
        auto range = mConts.firstEmptyRangeLowerBound(frame);
        return range.fMin;
    }

    int firstStaleOrEmptyFrameAtOrAfter(const int frame, const int maxFrame) const {
        for (int f = frame; f <= maxFrame; f++) {
            const auto cont = atFrame(f);
            if (!cont) return f;
        }
        return maxFrame + 1;
    }

    void add(const stdsptr<Cont>& cont) {
        cont->mCacheGeneration = mCacheGeneration;
        const auto ret = mConts.insert({cont->getRange(), cont});
        if(ret.second) mUsedRange.addIfInRange(cont.get());
    }

    void clear() {
        invalidateAll();
        mConts.clear();
    }

    void remove(const stdsptr<Cont>& cont) {
        mConts.erase(cont->getRange());
    }

    void remove(const iValueRange& range) {
        mConts.erase(range);
    }

    template <class T = Cont>
    T * atFrame(const int relFrame) const {
        const auto it = mConts.atFrame(relFrame);
        if(it == mConts.end()) return nullptr;
        const auto cont = static_cast<T*>(it->second.get());
        if(cont->mCacheGeneration != mCacheGeneration) return nullptr;
        return cont;
    }

    template <class T = Cont>
    stdsptr<T> sharedAtFrame(const int relFrame) const {
        const auto it = mConts.atFrame(relFrame);
        if(it == mConts.end()) return nullptr;
        const auto cont = std::static_pointer_cast<T>(it->second);
        if(cont->mCacheGeneration != mCacheGeneration) return nullptr;
        return cont;
    }

    template <class T = Cont>
    T * atOrBeforeFrame(const int relFrame) const {
        const auto it = mConts.atOrBeforeFrame(relFrame);
        if(it == mConts.end()) return nullptr;
        const auto cont = static_cast<T*>(it->second.get());
        if(cont->mCacheGeneration != mCacheGeneration) return nullptr;
        return cont;
    }

    void setUseRange(const iValueRange& range) {
        mUsedRange.replaceRange(range);
    }

    void setMaxUseRange(const int max) {
        mUsedRange.setRangeMax(max);
    }

    void setMinUseRange(const int min) {
        mUsedRange.setRangeMin(min);
    }

    void clearUseRange() {
        mUsedRange.clearRange();
    }

    void evictStaleEntries() {
        auto it = mConts.begin();
        while (it != mConts.end()) {
            if (it->second->mCacheGeneration != mCacheGeneration) {
                it = mConts.erase(it);
            } else {
                ++it;
            }
        }
    }

    int freeUnusedMemoryOutsideRange(const iValueRange &range,
                                     int maxToFree = INT_MAX);

    auto begin() const { return mConts.begin(); }
    auto end() const { return mConts.end(); }
private:
    RangeMap<stdsptr<Cont>> mConts;
    UsedRange mUsedRange;
    int mCacheGeneration = 0;
};

#endif // HddCACHABLECACHEHANDLER_H
