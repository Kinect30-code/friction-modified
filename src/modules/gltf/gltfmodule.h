#ifndef GLTFMODULE_H
#define GLTFMODULE_H

#include <QFileInfo>

#include "Boxes/boundingbox.h"

class Canvas;

namespace GltfModule {

bool supportsImport(const QFileInfo& fileInfo);
qsptr<BoundingBox> importFileAsBox(const QFileInfo& fileInfo,
                                   Canvas* const scene);
QString importFileFilter();

}

#endif // GLTFMODULE_H
