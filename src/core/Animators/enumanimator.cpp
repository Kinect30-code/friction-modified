#include "enumanimator.h"

EnumAnimator::EnumAnimator(const QString& name,
                           const QStringList& valueNames,
                           const int defaultValue)
    : IntAnimator(defaultValue,
                  0,
                  qMax(0, valueNames.count() - 1),
                  1,
                  name)
    , mValueNames(valueNames)
{
    setCurrentIntValue(qBound(0, defaultValue, qMax(0, valueNames.count() - 1)));
}

QString EnumAnimator::getCurrentValueName() const
{
    const int index = getCurrentValue();
    if(index < 0 || index >= mValueNames.count()) {
        return QStringLiteral("null");
    }
    return mValueNames.at(index);
}

void EnumAnimator::setCurrentValue(const int value)
{
    setCurrentIntValue(qBound(0, value, qMax(0, mValueNames.count() - 1)));
}

void EnumAnimator::setValueNames(const QStringList &valueNames)
{
    const QStringList safeNames = valueNames.isEmpty() ?
                QStringList() << QStringLiteral("None") :
                valueNames;
    mValueNames = safeNames;
    setIntValueRange(0, qMax(0, mValueNames.count() - 1));
    setCurrentIntValue(qBound(0, getCurrentIntValue(),
                              qMax(0, mValueNames.count() - 1)));
    emit valueNamesChanged(mValueNames);
}
