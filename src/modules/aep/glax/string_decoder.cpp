/*
 * SPDX-FileCopyrightText: 2019-2026 Mattia Basaglia <dev@dragon.best>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Qt5 port: glaxnimate uses QStringConverter (Qt6); here we use
 * QTextCodec which ships with Qt5.
 */
#include <QtGlobal>

#include "string_decoder.hpp"

#include <QTextCodec>

QString glaxnimate::io::aep::decode_string(const QByteArray& data)
{
    // Heuristic: AEP strings are UTF-8 in modern versions; if the data is
    // full of NUL bytes it is probably UTF-16.
    int nulCount = 0;
    for (int i = 0; i < data.size() && i < 64; ++i) {
        if (data.at(i) == '\0') { ++nulCount; }
    }
    if (nulCount > 4) {
        QTextCodec* u16 = QTextCodec::codecForName("UTF-16");
        if (u16) { return u16->toUnicode(data); }
    }
    return QString::fromUtf8(data);
}

QString glaxnimate::io::aep::decode_utf16(const QByteArray& data, bool big_endian)
{
    QTextCodec* codec = QTextCodec::codecForName(
                big_endian ? "UTF-16BE" : "UTF-16LE");
    if (codec) { return codec->toUnicode(data); }
    return QString::fromUtf8(data);
}
