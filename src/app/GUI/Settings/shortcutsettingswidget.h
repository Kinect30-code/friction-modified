#ifndef SHORTCUTSETTINGSWIDGET_H
#define SHORTCUTSETTINGSWIDGET_H

#include "widgets/settingswidget.h"

#include <QMap>

class QKeySequenceEdit;

class ShortcutSettingsWidget : public SettingsWidget
{
public:
    explicit ShortcutSettingsWidget(QWidget *parent = nullptr);

    void applySettings() override;
    void updateSettings(bool restore = false) override;

private:
    QMap<QString, QKeySequenceEdit*> mEditors;
};

#endif // SHORTCUTSETTINGSWIDGET_H
