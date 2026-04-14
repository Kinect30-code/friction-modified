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

#include "eimporters.h"

#include "GUI/mainwindow.h"
#include "svgimporter.h"
#include "pluginmanager.h"
#include "../modules/ora/oramodule.h"
#include "../modules/gltf/gltfmodule.h"

qsptr<BoundingBox> eXevImporter::import(const QFileInfo &fileInfo, Canvas * const scene) const {
    Q_UNUSED(scene);
    MainWindow::sGetInstance()->loadXevFile(fileInfo.absoluteFilePath());
    return nullptr;
}

qsptr<BoundingBox> evImporter::import(const QFileInfo &fileInfo, Canvas * const scene) const {
    Q_UNUSED(scene);
    MainWindow::sGetInstance()->loadEVFile(fileInfo.absoluteFilePath());
    return nullptr;
}

qsptr<BoundingBox> eSvgImporter::import(const QFileInfo &fileInfo, Canvas * const scene) const {
    const auto gradientCreator = [scene]() {
        return scene->createNewGradient();
    };
    return ImportSVG::loadSVGFile(fileInfo.absoluteFilePath(),
                                  gradientCreator);
}

qsptr<BoundingBox> eOraImporter::import(const QFileInfo &fileInfo, Canvas * const scene) const {
    if(!PluginManager::isEnabled(PluginFeature::oraImport)) {
        return nullptr;
    }
    return OraModule::importOraFileAsPrecomp(fileInfo, scene);
}

bool eGltfImporter::supports(const QFileInfo &fileInfo) const {
    return PluginManager::isEnabled(PluginFeature::glbViewer) &&
           GltfModule::supportsImport(fileInfo);
}

qsptr<BoundingBox> eGltfImporter::import(const QFileInfo &fileInfo,
                                         Canvas * const scene) const {
    if(!PluginManager::isEnabled(PluginFeature::glbViewer)) {
        return nullptr;
    }
    return GltfModule::importFileAsBox(fileInfo, scene);
}
