#include "shortcutsettingswidget.h"

#include "GUI/aeshortcutdefaults.h"
#include "appsupport.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QVBoxLayout>

ShortcutSettingsWidget::ShortcutSettingsWidget(QWidget *parent)
    : SettingsWidget(parent)
{
    const auto intro = new QLabel(tr("Edit the AE-style interaction shortcuts here. Some legacy actions may still require a restart to fully refresh."),
                                  this);
    intro->setWordWrap(true);
    addWidget(intro);

    QString currentSection;
    QGroupBox *currentGroup = nullptr;
    QFormLayout *currentLayout = nullptr;

    for (const auto &def : aeShortcutDefinitions()) {
        if (def.section != currentSection) {
            currentSection = def.section;
            currentGroup = new QGroupBox(currentSection, this);
            currentGroup->setObjectName(QStringLiteral("BlueBox"));
            currentLayout = new QFormLayout(currentGroup);
            currentLayout->setContentsMargins(10, 12, 10, 10);
            currentLayout->setHorizontalSpacing(8);
            currentLayout->setVerticalSpacing(6);
            addWidget(currentGroup);
        }

        if (!currentLayout) {
            continue;
        }

        const auto editor = new QKeySequenceEdit(currentGroup);
        editor->setToolTip(def.description);
        currentLayout->addRow(def.label, editor);
        mEditors.insert(def.id, editor);
    }

    addSeparator();
}

void ShortcutSettingsWidget::applySettings()
{
    for (auto it = mEditors.constBegin(); it != mEditors.constEnd(); ++it) {
        AppSupport::setSettings(QStringLiteral("shortcuts"),
                                it.key(),
                                it.value()->keySequence().toString(QKeySequence::NativeText));
    }
}

void ShortcutSettingsWidget::updateSettings(bool restore)
{
    for (const auto &def : aeShortcutDefinitions()) {
        const auto it = mEditors.find(def.id);
        if (it == mEditors.end()) {
            continue;
        }

        const QString sequence = restore
                ? def.defaultSequence
                : AppSupport::getSettings(QStringLiteral("shortcuts"),
                                          def.id,
                                          def.defaultSequence).toString();
        it.value()->setKeySequence(QKeySequence(sequence));
    }
}
