#include "gltfmodule.h"

#include "../../core/pluginmanager.h"

#include "glbbox.h"

namespace GltfModule {

bool supportsImport(const QFileInfo &fileInfo) {
    const QString suffix = fileInfo.suffix().toLower();
    return suffix == QStringLiteral("glb") || suffix == QStringLiteral("gltf");
}

qsptr<BoundingBox> importFileAsBox(const QFileInfo &fileInfo,
                                   Canvas * const scene) {
    Q_UNUSED(scene)
    if(!PluginManager::isEnabled(PluginFeature::glbViewer)) {
        return nullptr;
    }
    auto result = enve::make_shared<GlbBox>();
    result->setFilePath(fileInfo.absoluteFilePath());
    return result;
}

QString importFileFilter() {
    return QStringLiteral("GLB/glTF Files (*.glb *.gltf)");
}

}
