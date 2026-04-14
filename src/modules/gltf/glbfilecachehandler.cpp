#include "glbfilecachehandler.h"

#include <QFile>

#include "../../core/GUI/edialogs.h"

GlbFileCacheHandler::GlbFileCacheHandler() {}

void GlbFileCacheHandler::reload() {}

void GlbFileCacheHandler::replace() {
    const QString importPath = eDialogs::openFile(
                QStringLiteral("Replace GLB Source %1").arg(path()),
                path(),
                QStringLiteral("GLB/glTF Files (*.glb *.gltf)"));
    if(importPath.isEmpty()) {
        return;
    }
    if(!QFile::exists(importPath)) {
        return;
    }
    try {
        setPath(importPath);
    } catch(const std::exception& e) {
        gPrintExceptionCritical(e);
    }
}
