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
#include "Tasks/etask.h"
#include <QMutex>
#include <QMutexLocker>

namespace {

QList<HddCachableCont*> sTrackedTmpFiles;
QMutex sTrackedTmpMutex(QMutex::Recursive);
qint64 sTrackedTmpFileBytes = 0;

qint64 configuredHddCacheCapBytes() {
    const auto cap = eSettings::instance().fHddCacheMBCap;
    return cap.fValue > 0 ? longB(cap).fValue : 0;
}

bool tmpTaskCanStillProduceData(const stdsptr<eTask> &task) {
    if(!task) {
        return false;
    }
    switch(task->getState()) {
    case eTaskState::created:
    case eTaskState::qued:
    case eTaskState::processing:
    case eTaskState::waiting:
        return true;
    case eTaskState::finished:
    case eTaskState::canceled:
        return false;
    }
    return false;
}

}

HddCachableCont::HddCachableCont() {}

HddCachableCont::~HddCachableCont() noexcept {
    mTmpFile.reset();
}

int HddCachableCont::free_RAM_k() {
    cleanupFinishedTmpTasks();
    if(!storesDataInMemory()) {
        discardIfUnrecoverable();
        return 0;
    }
    // Queue a tmp-file save before releasing RAM so reclaim behaves like
    // cache spill instead of permanently dropping reusable frame/audio data.
    if(!mTmpFile && !mTmpSaveTask) {
        scheduleSaveToTmpFile();
    }
    const int bytes = clearMemory();
    setDataInMemory(false);
    discardIfUnrecoverable();
    return bytes;
}

eTask *HddCachableCont::scheduleDeleteTmpFile() {
    try {
        cleanupFinishedTmpTasks();
        if(!mTmpFile) {
            discardIfUnrecoverable();
            return nullptr;
        }
        const auto updatable = enve::make_shared<TmpDeleter>(mTmpFile);
        clearTrackedTmpFile();
        mTmpFile.reset();
        updatable->queTask();
        discardIfUnrecoverable();
        return updatable.get();
    } catch(const std::exception& e) {
        qWarning() << "scheduleDeleteTmpFile failed:" << e.what();
        return nullptr;
    } catch(...) {
        qWarning() << "scheduleDeleteTmpFile failed (unknown)";
        return nullptr;
    }
}

eTask *HddCachableCont::scheduleSaveToTmpFile() {
    cleanupFinishedTmpTasks();
    if(!eSettings::instance().fHddCache) {
        discardIfUnrecoverable();
        return nullptr;
    }
    if(tmpTaskCanStillProduceData(mTmpSaveTask) || mTmpFile) return nullptr;
    mTmpSaveTask = createTmpFileDataSaver();
    const auto thisRef = ref<HddCachableCont>();
    mTmpSaveTask->addDependent({nullptr, [thisRef]() {
                                    if(thisRef) thisRef->onTmpSaveTaskCanceled();
                                }});
    mTmpSaveTask->queTask();
    return mTmpSaveTask.get();
}

eTask *HddCachableCont::scheduleLoadFromTmpFile() {
    cleanupFinishedTmpTasks();
    if(storesDataInMemory()) return nullptr;
    if(tmpTaskCanStillProduceData(mTmpLoadTask)) return mTmpLoadTask.get();
    if(!tmpTaskCanStillProduceData(mTmpSaveTask) && !mTmpFile) {
        discardIfUnrecoverable();
        return nullptr;
    }
    if(mTmpFile) {
        touchTrackedTmpFile();
    }

    mTmpLoadTask = createTmpFileDataLoader();
    const auto thisRef = ref<HddCachableCont>();
    mTmpLoadTask->addDependent({nullptr, [thisRef]() {
                                    if(thisRef) thisRef->onTmpLoadTaskCanceled();
                                }});
    if(tmpTaskCanStillProduceData(mTmpSaveTask))
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

bool HddCachableCont::hasRecoverableData() const {
    return mDataInMemory ||
           mTmpFile ||
           tmpTaskCanStillProduceData(mTmpLoadTask) ||
           tmpTaskCanStillProduceData(mTmpSaveTask);
}

void HddCachableCont::cleanupFinishedTmpTasks() {
    if(mTmpLoadTask && !tmpTaskCanStillProduceData(mTmpLoadTask)) {
        mTmpLoadTask.reset();
    }
    if(mTmpSaveTask && !tmpTaskCanStillProduceData(mTmpSaveTask)) {
        mTmpSaveTask.reset();
    }
}

void HddCachableCont::discardIfUnrecoverable() noexcept {
    cleanupFinishedTmpTasks();
    if(!mDataInMemory && !mTmpFile && !mTmpLoadTask && !mTmpSaveTask) {
        try { noDataLeft_k(); } catch(...) {}
    }
}

void HddCachableCont::onTmpSaveTaskCanceled() {
    mTmpSaveTask.reset();
    discardIfUnrecoverable();
}

void HddCachableCont::onTmpLoadTaskCanceled() {
    mTmpLoadTask.reset();
    discardIfUnrecoverable();
}

void HddCachableCont::clearTrackedTmpFile() {
    QMutexLocker lock(&sTrackedTmpMutex);
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
    QMutexLocker lock(&sTrackedTmpMutex);
    sTrackedTmpFiles.removeAll(this);
    sTrackedTmpFiles.append(this);
}

void HddCachableCont::trackTmpFile(qint64 tmpFileBytes) {
    clearTrackedTmpFile();
    QMutexLocker lock(&sTrackedTmpMutex);
    mTmpFileBytes = qMax<qint64>(0, tmpFileBytes);
    sTrackedTmpFileBytes += mTmpFileBytes;
    sTrackedTmpFiles.append(this);
}

void HddCachableCont::trimTrackedTmpFiles(HddCachableCont *protectedCont) {
    const qint64 capBytes = configuredHddCacheCapBytes();
    if(capBytes <= 0) {
        return;
    }

    sTrackedTmpMutex.lock();
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
        sTrackedTmpMutex.unlock();
        cont->scheduleDeleteTmpFile();
        sTrackedTmpMutex.lock();
        scanBudget = sTrackedTmpFiles.count();
    }
    sTrackedTmpMutex.unlock();
}
