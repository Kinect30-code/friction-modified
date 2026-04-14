#ifndef ENUMANIMATOR_H
#define ENUMANIMATOR_H

#include "intanimator.h"

class CORE_EXPORT EnumAnimator : public IntAnimator {
    Q_OBJECT
    e_OBJECT
public:
    EnumAnimator(const QString& name,
                 const QStringList& valueNames,
                 const int defaultValue = 0);

    const QStringList& getValueNames() const { return mValueNames; }
    QString getCurrentValueName() const;

    int getCurrentValue() const { return getCurrentIntValue(); }
    int getCurrentValue(const qreal relFrame) const { return getEffectiveIntValue(relFrame); }
    void setCurrentValue(const int value);
    void setValueNames(const QStringList& valueNames);
signals:
    void valueNamesChanged(const QStringList& valueNames);
private:
    QStringList mValueNames;
};

#endif // ENUMANIMATOR_H
