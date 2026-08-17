/*
 * PSD import module for friction-modified.
 *
 * Maps a parsed PSD document onto friction's scene graph: each layer
 * becomes an ImageBox inside a nested composition, preserving layer
 * order, visibility, opacity and blend mode.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSDMAPPER_H
#define PSDMAPPER_H

#include <QFileInfo>
#include <QString>

#include "smartPointers/selfref.h"

class BoundingBox;
class Canvas;

namespace PsdModule {

// Imports a layered PSD file as a nested composition.
// Returns an InternalLinkCanvas referencing the imported scene, or nullptr.
qsptr<BoundingBox> importPsdFile(const QFileInfo& fileInfo,
                                 Canvas* const scene);

} // namespace PsdModule

#endif // PSDMAPPER_H
