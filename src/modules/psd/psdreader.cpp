/*
 * PSD (Adobe Photoshop) layered import for friction-modified.
 *
 * Implements the subset of the Photoshop file format needed for layered
 * imports: file header, layer & mask section, layer records (name, blend
 * mode, opacity, visibility, bounds) and per-layer channel data with both
 * raw and RLE compression. Each layer is decoded to an ARGB32 QImage.
 *
 * Format reference: Adobe Photoshop File Formats Specification.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "psdreader.h"

#include <QBuffer>
#include <QDataStream>
#include <QIODevice>

namespace PsdModule {

namespace {

const quint32 kPsdSignature = 0x38425053; // "8BPS"

quint16 readU16(QDataStream& s)
{
    quint16 v = 0;
    s >> v;
    return v;
}

quint32 readU32(QDataStream& s)
{
    quint32 v = 0;
    s >> v;
    return v;
}

qint32 readS32(QDataStream& s)
{
    qint32 v = 0;
    s >> v;
    return v;
}

QString readPascalString(QDataStream& s)
{
    quint8 len = 0;
    s >> len;
    QByteArray raw(len, '\0');
    if (len > 0) {
        s.readRawData(raw.data(), len);
    }
    // Total is 1 (len byte) + len, padded to even.
    const int total = 1 + int(len);
    if (total & 1) { s.skipRawData(1); }
    return QString::fromLatin1(raw);
}

QString readUnicodeString(QDataStream& s)
{
    quint32 len = 0;
    s >> len;
    QString str;
    for (quint32 i = 0; i < len; ++i) {
        quint16 ch = 0;
        s >> ch;
        str += QChar(ch);
    }
    // 4-byte length + 2*len bytes, padded to multiple of 4.
    const int total = 4 + int(len) * 2;
    const int pad = (4 - (total % 4)) % 4;
    if (pad > 0) { s.skipRawData(pad); }
    return str;
}

// RLE decode for a single channel row.
// PackBits-style: control byte n<128 → copy n+1 bytes; n>128 → repeat next
// byte 257-n times.
bool decodeRleRow(QDataStream& s, quint8* out, int rowBytes)
{
    int written = 0;
    int noopCount = 0;
    while (written < rowBytes) {
        qint8 ctrl = 0;
        s >> ctrl;
        if (s.status() != QDataStream::Ok) { return false; }
        if (ctrl >= 0) {
            const int count = int(ctrl) + 1;
            if (written + count > rowBytes) { return false; }
            if (s.readRawData(reinterpret_cast<char*>(out + written), count) != count) {
                return false;
            }
            written += count;
            noopCount = 0;
        } else if (ctrl != -128) {
            // Negative control: repeat the next byte (1 - ctrl) times.
            const int count = 1 - int(ctrl);
            if (count <= 0 || written + count > rowBytes) { return false; }
            quint8 val = 0;
            s >> val;
            for (int i = 0; i < count; ++i) { out[written + i] = val; }
            written += count;
            noopCount = 0;
        } else {
            // -128 is a no-op; guard against pathological data.
            if (++noopCount > rowBytes) { return false; }
        }
    }
    return written == rowBytes;
}

// Reads a channel's packed data into `pixels` (channelBytes total for the
// whole layer bounds, one byte per pixel). `isRle` tells whether the channel
// uses RLE (decided by the caller from the channel length vs raw size).
bool readChannelData(QDataStream& s,
                     const PsdLayer& layer,
                     int channelBytes,
                     bool isRle,
                     QByteArray& pixels)
{
    if (channelBytes <= 0) { return true; }
    const int rowBytes = layer.right - layer.left;
    if (rowBytes <= 0) { return false; }
    const int rows = layer.bottom - layer.top;
    if (rows <= 0) { return false; }

    pixels.resize(channelBytes);
    if (isRle) {
        QVector<quint16> rowLens(rows);
        for (int r = 0; r < rows; ++r) { rowLens[r] = readU16(s); }
        quint8* out = reinterpret_cast<quint8*>(pixels.data());
        for (int r = 0; r < rows; ++r) {
            if (!decodeRleRow(s, out + r * rowBytes, rowBytes)) {
                return false;
            }
        }
        return true;
    }
    // Raw data.
    const int toRead = qMin(channelBytes, int(s.device()->bytesAvailable()));
    if (s.readRawData(pixels.data(), toRead) != toRead) { return false; }
    return true;
}

// Builds an ARGB32 image from up to 4 channels (A,R,G,B) in PSD channel
// order (typically R,G,B,A).
QImage channelsToImage(const QByteArray& rCh, const QByteArray& gCh,
                       const QByteArray& bCh, const QByteArray& aCh,
                       const PsdLayer& layer)
{
    const int w = layer.right - layer.left;
    const int h = layer.bottom - layer.top;
    if (w <= 0 || h <= 0) { return QImage(); }

    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    const int n = w * h;
    const bool hasR = rCh.size() >= n;
    const bool hasG = gCh.size() >= n;
    const bool hasB = bCh.size() >= n;
    const bool hasA = aCh.size() >= n;

    const quint8* rp = hasR ? reinterpret_cast<const quint8*>(rCh.constData()) : nullptr;
    const quint8* gp = hasG ? reinterpret_cast<const quint8*>(gCh.constData()) : nullptr;
    const quint8* bp = hasB ? reinterpret_cast<const quint8*>(bCh.constData()) : nullptr;
    const quint8* ap = hasA ? reinterpret_cast<const quint8*>(aCh.constData()) : nullptr;

    for (int i = 0; i < n; ++i) {
        const int r = rp ? rp[i] : 0;
        const int g = gp ? gp[i] : 0;
        const int b = bp ? bp[i] : 0;
        const int a = ap ? ap[i] : 255;
        const int x = i % w;
        const int y = i / w;
        img.setPixel(x, y, qRgba(r, g, b, a));
    }
    return img;
}

} // namespace

bool readPsd(QIODevice* device, PsdDocument* out, QString* error)
{
    if (!device || !out) {
        if (error) { *error = QStringLiteral("Invalid arguments."); }
        return false;
    }
    if (!device->isOpen()) {
        if (!device->open(QIODevice::ReadOnly)) {
            if (error) { *error = QStringLiteral("Cannot open PSD file."); }
            return false;
        }
    }

    QDataStream s(device);
    s.setByteOrder(QDataStream::BigEndian);

    const quint32 sig = readU32(s);
    if (sig != kPsdSignature) {
        if (error) { *error = QStringLiteral("Not a PSD file (bad signature)."); }
        return false;
    }

    const quint16 version = readU16(s);
    if (version != 1) {
        if (error) { *error = QStringLiteral("Unsupported PSD version."); }
        return false;
    }

    s.skipRawData(6); // reserved
    const quint16 channels = readU16(s);
    const quint32 height = readU32(s);
    const quint32 width = readU32(s);
    const quint16 depth = readU16(s);
    const quint16 colorMode = readU16(s);
    out->width = int(width);
    out->height = int(height);
    out->depth = int(depth);
    out->colorMode = int(colorMode);

    // Skip color mode data.
    const quint32 colorModeLen = readU32(s);
    s.skipRawData(int(colorModeLen));

    // Skip image resources.
    const quint32 imgResLen = readU32(s);
    s.skipRawData(int(imgResLen));

    // Layer & mask info.
    const quint32 layerMaskLen = readU32(s);
    const qint64 sectionEnd = s.device()->pos() + qint64(layerMaskLen);
    if (layerMaskLen == 0) {
        if (error) { *error = QStringLiteral("PSD has no layers."); }
        return false;
    }

    const quint32 layerInfoLen = readU32(s);
    const qint64 layerInfoEnd = s.device()->pos() + qint64(layerInfoLen);

    const qint16 layerCountRaw = readS32(s);
    // Negative count means the first layer is a clipping group base.
    int layerCount = layerCountRaw < 0 ? -layerCountRaw : layerCountRaw;
    if (layerCount > 4096) {
        if (error) { *error = QStringLiteral("Suspicious layer count."); }
        return false;
    }

    out->layers.reserve(layerCount);
    for (int i = 0; i < layerCount; ++i) {
        PsdLayer layer;
        layer.top = readS32(s);
        layer.left = readS32(s);
        layer.bottom = readS32(s);
        layer.right = readS32(s);
        if (layer.right <= layer.left || layer.bottom <= layer.top) { continue; }

        // Channel references: id is 2 bytes, length is 4 bytes.
        const quint16 chCount = readU16(s);
        struct ChanRef { qint16 id; qint32 len; };
        QVector<ChanRef> chans(chCount);
        for (int c = 0; c < chCount; ++c) {
            qint16 cid = 0;
            s >> cid;
            chans[c].id = cid;
            chans[c].len = readS32(s);
        }

        // Blend mode signature "8BIM" then 4-char mode.
        const quint32 blendSig = readU32(s);
        Q_UNUSED(blendSig)
        QByteArray mode(4, '\0');
        s.readRawData(mode.data(), 4);
        layer.blendMode = QString::fromLatin1(mode);
        layer.opacity = int(readU8(s));
        quint8 clipping = 0;
        s >> clipping;
        Q_UNUSED(clipping)
        quint8 flags = 0;
        s >> flags;
        layer.visible = !(flags & 0x2); // bit 1 = hidden
        s.skipRawData(1); // filler

        // Extra data length. Layout: Pascal layer name + 4-byte aligned,
        // then a series of tagged blocks (luni = unicode name, etc).
        const quint32 extraLen = readU32(s);
        const qint64 extraStart = s.device()->pos();
        const qint64 extraEnd = extraStart + qint64(extraLen);

        // Pascal layer name (ASCII/Latin1).
        layer.name = readPascalString(s);

        // Parse tagged extra blocks for the unicode name.
        while (s.device()->pos() + 12 <= extraEnd) {
            QByteArray sig2(4, '\0');
            s.readRawData(sig2.data(), 4);
            QByteArray key(4, '\0');
            s.readRawData(key.data(), 4);
            const quint32 blockLen = readU32(s);
            const qint64 blockEnd = s.device()->pos() + qint64(blockLen);
            if (key == "luni" && s.device()->bytesAvailable() >= 4) {
                const QString uname = readUnicodeString(s);
                if (!uname.isEmpty()) { layer.name = uname; }
            }
            s.device()->seek(blockEnd);
            if (blockLen & 1) { s.device()->seek(s.device()->pos() + 1); }
        }
        s.device()->seek(extraEnd);

        // Channel data (after the extra data).
        const int w = layer.right - layer.left;
        const int h = layer.bottom - layer.top;
        const int channelBytes = w * h;

        QByteArray rCh, gCh, bCh, aCh;
        for (const auto& ch : chans) {
            if (ch.len <= 0) { continue; }
            QByteArray pixels;
            const int rawSize = channelBytes;
            const bool isRle = ch.len > rawSize; // RLE adds row-table bytes
            if (!readChannelData(s, layer, channelBytes, isRle, pixels)) {
                continue;
            }
            switch (ch.id) {
            case 0: rCh = pixels; break;
            case 1: gCh = pixels; break;
            case 2: bCh = pixels; break;
            case -1: aCh = pixels; break;
            default: break;
            }
        }

        layer.image = channelsToImage(rCh, gCh, bCh, aCh, layer);
        out->layers.append(layer);
    }

    s.device()->seek(layerInfoEnd);

    // Global layer mask info - skip.
    s.device()->seek(sectionEnd);

    // (Image data at the end is the flattened composite; not needed.)
    return true;
}

quint8 PsdModule::readU8(QDataStream& s)
{
    quint8 v = 0;
    s >> v;
    return v;
}

} // namespace PsdModule
