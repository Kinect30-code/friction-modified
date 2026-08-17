/*
 * AEP import support for friction-modified.
 *
 * Thin adapter replacing glaxnimate's io framework. The AEP parser only
 * needs ImportExport::warning() to surface non-fatal issues.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <QString>

#include <QDebug>

namespace glaxnimate::io {

class ImportExport
{
public:
    void warning(const QString& msg)
    {
        qWarning() << "[aep-import]" << msg;
    }
};

} // namespace glaxnimate::io
