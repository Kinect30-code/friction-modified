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

#include "tmpsaver.h"

#include <QDir>
#include <QFileInfo>

#include "../appsupport.h"
#include "../Private/esettings.h"

namespace {

QString hddCacheDirectory() {
    QString path = eSettings::instance().fHddCacheFolder.trimmed();
    if(path.isEmpty()) {
        path = QDir(AppSupport::getAppTempPath()).
                filePath(QStringLiteral("friction-hdd-cache"));
    }

    QDir dir(path);
    if(dir.exists() || dir.mkpath(QStringLiteral("."))) {
        return dir.filePath(QStringLiteral("friction-XXXXXX.tmp"));
    }

    return QString();
}

}

TmpSaver::TmpSaver(HddCachableCont* const target) :
    mTarget(target) {}

void TmpSaver::process() {
    const QString fileTemplate = hddCacheDirectory();
    mTmpFile = fileTemplate.isEmpty() ?
                qsptr<QTemporaryFile>(new QTemporaryFile()) :
                qsptr<QTemporaryFile>(new QTemporaryFile(fileTemplate));
    if(mTmpFile->open()) {
        eWriteStream dst(mTmpFile.get());
        write(dst);
        mTmpFile->close();
        mSavingSuccessful = true;
    } else {
        mSavingSuccessful = false;
    }
}

void TmpSaver::afterProcessing() {
    if(!mTarget) return;
    if(!mSavingSuccessful) return;
    mTarget->setDataSavedToTmpFile(mTmpFile,
                                   QFileInfo(mTmpFile->fileName()).size());
}
