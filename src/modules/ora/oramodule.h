#ifndef ORAMODULE_H
#define ORAMODULE_H

#include <QFileInfo>

#include "Boxes/boundingbox.h"

class Canvas;

namespace OraModule {

qsptr<BoundingBox> importOraFileAsGroup(const QFileInfo& fileInfo);
qsptr<BoundingBox> importOraFileAsPrecomp(const QFileInfo& fileInfo,
                                          Canvas* const scene);
QList<int> sceneNavigationChainIds(const Canvas* scene);

}

#endif // ORAMODULE_H
