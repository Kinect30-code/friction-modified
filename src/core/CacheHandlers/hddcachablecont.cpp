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

#include "hddcachablecont.h"
#include "../Private/esettings.h"

namespace {

QList<HddCachableCont*> sTrackedTmpFiles;
qint64 sTrackedTmpFileBytes = 0;

qint64 configuredHddCacheCapBytes() {
    const auto cap = eSettings::instance().fHddCacheMBCap;
    return cap.fValue > 0 ? longB(cap).fValue : 0;
}

}

HddCachableCont::HddCachableCont() {}

HddCachableCont::~HddCachableCont() {
    if(mTmpFile) scheduleDeleteTmpFile();
}

int HddCachableCont::free_RAM_k() {
    if(!storesDataInMemory()) return 0;
    // Queue a tmp-file save before releasing RAM so reclaim behaves like
    // cache spill instead of permanently dropping reusable frame/audio data.
    if(!mTmpFile && !mTmpSaveTask) {
        scheduleSaveToTmpFile();
    }
    const int bytes = clearMemory();
    setDataInMemory(false);
    if(!mTmpFile && !mTmpSaveTask) noDataLeft_k();
    return bytes;
}

eTask *HddCachableCont::scheduleDeleteTmpFile() {
    if(!mTmpFile) return nullptr;
    const auto updatable = enve::make_shared<TmpDeleter>(mTmpFile);
    clearTrackedTmpFile();
    mTmpFile.reset();
    updatable->queTask();
    return updatable.get();
}

eTask *HddCachableCont::scheduleSaveToTmpFile() {
    if(!eSettings::instance().fHddCache) return nullptr;
    if(mTmpSaveTask || mTmpFile) return nullptr;
    mTmpSaveTask = createTmpFileDataSaver();
    mTmpSaveTask->queTask();
    return mTmpSaveTask.get();
}

eTask *HddCachableCont::scheduleLoadFromTmpFile() {
    if(storesDataInMemory()) return nullptr;
    if(mTmpLoadTask) return mTmpLoadTask.get();
    if(!mTmpSaveTask && !mTmpFile) return nullptr;
    if(mTmpFile) {
        touchTrackedTmpFile();
    }

    mTmpLoadTask = createTmpFileDataLoader();
    if(mTmpSaveTask)
        mTmpSaveTask->addDependent(mTmpLoadTask.get());
    mTmpLoadTask->queTask();
    return mTmpLoadTask.get();
}

void HddCachableCont::setDataSavedToTmpFile(const qsptr<QTemporaryFile> &tmpFile,
                                            qint64 tmpFileBytes) {
    mTmpSaveTask.reset();
    mTmpFile = tmpFile;
    trackTmpFile(tmpFileBytes);
    trimTrackedTmpFiles(this);
}

void HddCachableCont::afterDataLoadedFromTmpFile() {
    setDataInMemory(true);
    mTmpLoadTask.reset();
    if(!inUse()) addToMemoryManagment();
}

void HddCachableCont::afterDataReplaced() {
    setDataInMemory(true);
    updateInMemoryManagment();
    if(mTmpFile) scheduleDeleteTmpFile();
}

void HddCachableCont::setDataInMemory(const bool dataInMemory) {
    mDataInMemory = dataInMemory;
}

void HddCachableCont::clearTrackedTmpFile() {
    if(mTmpFileBytes > 0) {
        sTrackedTmpFileBytes = qMax<qint64>(0, sTrackedTmpFileBytes - mTmpFileBytes);
        mTmpFileBytes = 0;
    }
    sTrackedTmpFiles.removeAll(this);
}

void HddCachableCont::touchTrackedTmpFile() {
    if(!mTmpFile) {
        return;
    }
    sTrackedTmpFiles.removeAll(this);
    sTrackedTmpFiles.append(this);
}

void HddCachableCont::trackTmpFile(qint64 tmpFileBytes) {
    clearTrackedTmpFile();
    mTmpFileBytes = qMax<qint64>(0, tmpFileBytes);
    sTrackedTmpFileBytes += mTmpFileBytes;
    sTrackedTmpFiles.append(this);
}

void HddCachableCont::trimTrackedTmpFiles(HddCachableCont *protectedCont) {
    const qint64 capBytes = configuredHddCacheCapBytes();
    if(capBytes <= 0) {
        return;
    }

    int scanBudget = sTrackedTmpFiles.count();
    while(sTrackedTmpFileBytes > capBytes &&
          scanBudget > 0 &&
          !sTrackedTmpFiles.isEmpty()) {
        auto * const cont = sTrackedTmpFiles.takeFirst();
        if(!cont) {
            scanBudget--;
            continue;
        }
        if(!cont->mTmpFile) {
            cont->clearTrackedTmpFile();
            scanBudget = sTrackedTmpFiles.count();
            continue;
        }
        if(cont == protectedCont) {
            sTrackedTmpFiles.append(cont);
            scanBudget--;
            continue;
        }
        cont->scheduleDeleteTmpFile();
        scanBudget = sTrackedTmpFiles.count();
    }
}
