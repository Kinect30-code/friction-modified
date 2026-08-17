/*
 * PSD (Adobe Photoshop) layered import for friction-modified.
 *
 * Minimal PSD parser: reads the layer record structure (names, blend mode,
 * opacity, visibility, bounds) and decodes each layer's pixel data into a
 * QImage, which the mapper turns into an ImageBox.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef PSDREADER_H
#define PSDREADER_H

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>

class QIODevice;
class QDataStream;

namespace PsdModule {

struct PsdLayer
{
    QString name;
    QString blendMode;     // "norm", "mul ", "scrn", ...
    int opacity = 255;     // 0..255
    bool visible = true;
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    QImage image;          // decoded pixels (ARGB32)
};

struct PsdDocument
{
    int width = 0;
    int height = 0;
    int colorMode = 0;     // 3=RGB, 1=grayscale, 2=indexed, 4=CMYK
    int depth = 8;         // bits per channel
    QVector<PsdLayer> layers;   // top-most first (PSD order)
};

// Parses a PSD file. Returns true on success and fills `out`.
bool readPsd(QIODevice* device, PsdDocument* out, QString* error);

// Internal helper shared with the implementation.
quint8 readU8(QDataStream& s);

} // namespace PsdModule

#endif // PSDREADER_H
