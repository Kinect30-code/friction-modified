#ifndef AESHORTCUTDEFAULTS_H
#define AESHORTCUTDEFAULTS_H

#include <QList>
#include <QKeySequence>
#include <QString>

struct AeShortcutDefinition {
    QString id;
    QString section;
    QString label;
    QString description;
    QString defaultSequence;
};

QList<AeShortcutDefinition> aeShortcutDefinitions();
QString aeShortcutDefault(const QString &id,
                         const QString &fallback = QString());
QKeySequence aeShortcutSequence(const QString &id,
                                const QString &fallback = QString());

#endif // AESHORTCUTDEFAULTS_H
