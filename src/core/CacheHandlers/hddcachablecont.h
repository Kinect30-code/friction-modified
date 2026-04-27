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

#ifndef HddCACHABLECONT_H
#define HddCACHABLECONT_H
#include "cachecontainer.h"
#include "tmpdeleter.h"
class eTask;

class CORE_EXPORT HddCachableCont : public CacheContainer {
protected:
    HddCachableCont();
    virtual int clearMemory() = 0;
    virtual stdsptr<eHddTask> createTmpFileDataSaver() = 0;
    virtual stdsptr<eHddTask> createTmpFileDataLoader() = 0;
public:
    ~HddCachableCont() noexcept;

    int free_RAM_k() final;
    bool hasRecoverableData() const;

    eTask* scheduleDeleteTmpFile();
    eTask* scheduleSaveToTmpFile();
    eTask* scheduleLoadFromTmpFile();

    void setDataSavedToTmpFile(const qsptr<QTemporaryFile> &tmpFile,
                               qint64 tmpFileBytes);

    bool storesDataInMemory() const { return mDataInMemory; }
    qsptr<QTemporaryFile> getTmpFile() const { return mTmpFile; }
protected:
    void afterDataLoadedFromTmpFile();
    void afterDataReplaced();
    void setDataInMemory(const bool dataInMemory);

    qsptr<QTemporaryFile> mTmpFile;
private:
    void cleanupFinishedTmpTasks();
    void discardIfUnrecoverable() noexcept;
    void onTmpSaveTaskCanceled();
    void onTmpLoadTaskCanceled();
    void clearTrackedTmpFile();
    void touchTrackedTmpFile();
    void trackTmpFile(qint64 tmpFileBytes);
    static void trimTrackedTmpFiles(HddCachableCont *protectedCont);

    bool mDataInMemory = false;
    qint64 mTmpFileBytes = 0;
    stdsptr<eTask> mTmpLoadTask;
    stdsptr<eTask> mTmpSaveTask;
};

#endif // HddCACHABLECONT_H
