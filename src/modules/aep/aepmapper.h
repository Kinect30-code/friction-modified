/*
 * AEP import module for friction-modified.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef AEPMAPPER_H
#define AEPMAPPER_H

#include <QFileInfo>
#include <QString>

#include "smartPointers/selfref.h"

class BoundingBox;
class Canvas;

namespace AepModule {

// Imports an Adobe After Effects project as a nested composition.
// Returns an InternalLinkCanvas referencing the imported scene, or nullptr.
qsptr<BoundingBox> importAepFile(const QFileInfo& fileInfo,
                                 Canvas* const scene);

} // namespace AepModule

#endif // AEPMAPPER_H
