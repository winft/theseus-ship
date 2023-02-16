/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2009 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2020 Cyril Rossi <cyril.rossi@enioka.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#include "kwintabboxconfigform.h"
#include "ui_main.h"

#include <QDebug>

#include <KActionCollection>
#include <KGlobalAccel>
#include <KShortcutsEditor>


namespace KWin
{

using namespace win;

KWinTabBoxConfigForm::KWinTabBoxConfigForm(TabboxType type, QWidget *parent)
    : QWidget(parent)
    , m_type(type)
    , ui(new Ui::KWinTabBoxConfigForm)
{
    ui->setupUi(this);

    ui->effectConfigButton->setIcon(QIcon::fromTheme(QStringLiteral("view-preview")));

    if (QApplication::screens().count() < 2) {
        ui->filterScreens->hide();
        ui->screenFilter->hide();
    }

    connect(ui->effectConfigButton, &QPushButton::clicked, this, &KWinTabBoxConfigForm::effectConfigButtonClicked);

    connect(ui->kcfg_ShowTabBox, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::tabBoxToggled);

    connect(ui->filterScreens, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onFilterScreen);
    connect(ui->currentScreen, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onFilterScreen);
    connect(ui->otherScreens, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onFilterScreen);

    connect(ui->filterDesktops, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onFilterDesktop);
    connect(ui->currentDesktop, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onFilterDesktop);
    connect(ui->otherDesktops, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onFilterDesktop);

    connect(ui->filterMinimization, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onFilterMinimization);
    connect(ui->visibleWindows, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onFilterMinimization);
    connect(ui->hiddenWindows, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onFilterMinimization);

    connect(ui->oneAppWindow, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onApplicationMode);
    connect(ui->showDesktop, &QAbstractButton::clicked, this, &KWinTabBoxConfigForm::onShowDesktopMode);

    connect(ui->switchingModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &KWinTabBoxConfigForm::onSwitchingMode);
    connect(ui->effectCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &KWinTabBoxConfigForm::onEffectCombo);

    auto addShortcut = [this](const char *name, KKeySequenceWidget *widget, const QKeySequence &sequence = QKeySequence()) {
        QAction *action = m_actionCollection->addAction(name);
        action->setProperty("isConfigurationAction", true);
        action->setText(i18n(name));

        m_actionCollection->setDefaultShortcut(action, sequence);

        widget->setCheckActionCollections({m_actionCollection});
        widget->setProperty("shortcutAction", name);
        connect(widget, &KKeySequenceWidget::keySequenceChanged, this, &KWinTabBoxConfigForm::onShortcutChanged);
    };

    // Shortcut config. The shortcut belongs to the component "kwin"!
    m_actionCollection = new KActionCollection(this, QStringLiteral("kwin"));
    m_actionCollection->setComponentDisplayName(i18n("KWin"));
    m_actionCollection->setConfigGroup("Navigation");
    m_actionCollection->setConfigGlobal(true);

    if (TabboxType::Main == m_type) {
        addShortcut("Walk Through Windows", ui->scAll, static_cast<Qt::Key>(Qt::ALT) + Qt::Key_Tab);
        addShortcut("Walk Through Windows (Reverse)",
                    ui->scAllReverse,
                    static_cast<Qt::Key>(Qt::ALT + Qt::SHIFT) + Qt::Key_Backtab);
        addShortcut("Walk Through Windows of Current Application",
                    ui->scCurrent,
                    static_cast<Qt::Key>(Qt::ALT) + Qt::Key_QuoteLeft);
        addShortcut("Walk Through Windows of Current Application (Reverse)",
                    ui->scCurrentReverse,
                    static_cast<Qt::Key>(Qt::ALT) + Qt::Key_AsciiTilde);
    } else if (TabboxType::Alternative == m_type) {
        addShortcut("Walk Through Windows Alternative", ui->scAll);
        addShortcut("Walk Through Windows Alternative (Reverse)", ui->scAllReverse);
        addShortcut("Walk Through Windows of Current Application Alternative", ui->scCurrent);
        addShortcut("Walk Through Windows of Current Application Alternative (Reverse)", ui->scCurrentReverse);
    }
}

KWinTabBoxConfigForm::~KWinTabBoxConfigForm()
{
    delete ui;
}

bool KWinTabBoxConfigForm::highlightWindows() const
{
    return ui->kcfg_HighlightWindows->isChecked();
}

bool KWinTabBoxConfigForm::showTabBox() const
{
    return ui->kcfg_ShowTabBox->isChecked();
}

int KWinTabBoxConfigForm::filterScreen() const
{
    if (ui->filterScreens->isChecked()) {
        return ui->currentScreen->isChecked() ? tabbox_config::OnlyCurrentScreenClients : tabbox_config::ExcludeCurrentScreenClients;
    } else {
        return tabbox_config::IgnoreMultiScreen;
    }
}

int KWinTabBoxConfigForm::filterDesktop() const
{
    if (ui->filterDesktops->isChecked()) {
        return ui->currentDesktop->isChecked() ? tabbox_config::OnlyCurrentDesktopClients : tabbox_config::ExcludeCurrentDesktopClients;
    } else {
        return tabbox_config::AllDesktopsClients;
    }
}

int KWinTabBoxConfigForm::filterMinimization() const
{
    if (ui->filterMinimization->isChecked()) {
        return ui->visibleWindows->isChecked() ? tabbox_config::ExcludeMinimizedClients : tabbox_config::OnlyMinimizedClients;
    } else {
        return tabbox_config::IgnoreMinimizedStatus;
    }
}

int KWinTabBoxConfigForm::applicationMode() const
{
    return ui->oneAppWindow->isChecked() ? tabbox_config::OneWindowPerApplication : tabbox_config::AllWindowsAllApplications;
}

int KWinTabBoxConfigForm::showDesktopMode() const
{
    return ui->showDesktop->isChecked() ? tabbox_config::ShowDesktopClient : tabbox_config::DoNotShowDesktopClient;
}

int KWinTabBoxConfigForm::switchingMode() const
{
    return ui->switchingModeCombo->currentIndex();
}

QString KWinTabBoxConfigForm::layoutName() const
{
    return ui->effectCombo->currentData().toString();
}

void KWinTabBoxConfigForm::setFilterScreen(win::tabbox_config::ClientMultiScreenMode mode)
{
    ui->filterScreens->setChecked(mode != tabbox_config::IgnoreMultiScreen);
    ui->currentScreen->setChecked(mode == tabbox_config::OnlyCurrentScreenClients);
    ui->otherScreens->setChecked(mode == tabbox_config::ExcludeCurrentScreenClients);
}

void KWinTabBoxConfigForm::setFilterDesktop(win::tabbox_config::ClientDesktopMode mode)
{
    ui->filterDesktops->setChecked(mode != tabbox_config::AllDesktopsClients);
    ui->currentDesktop->setChecked(mode == tabbox_config::OnlyCurrentDesktopClients);
    ui->otherDesktops->setChecked(mode == tabbox_config::ExcludeCurrentDesktopClients);
}

void KWinTabBoxConfigForm::setFilterMinimization(win::tabbox_config::ClientMinimizedMode mode)
{
    ui->filterMinimization->setChecked(mode != tabbox_config::IgnoreMinimizedStatus);
    ui->visibleWindows->setChecked(mode == tabbox_config::ExcludeMinimizedClients);
    ui->hiddenWindows->setChecked(mode == tabbox_config::OnlyMinimizedClients);
}

void KWinTabBoxConfigForm::setApplicationMode(win::tabbox_config::ClientApplicationsMode mode)
{
    ui->oneAppWindow->setChecked(mode == tabbox_config::OneWindowPerApplication);
}

void KWinTabBoxConfigForm::setShowDesktopMode(win::tabbox_config::ShowDesktopMode mode)
{
    ui->showDesktop->setChecked(mode == tabbox_config::ShowDesktopClient);
}

void KWinTabBoxConfigForm::setSwitchingModeChanged(win::tabbox_config::ClientSwitchingMode mode)
{
    ui->switchingModeCombo->setCurrentIndex(mode);
}

void KWinTabBoxConfigForm::setLayoutName(const QString &layoutName)
{
    ui->effectCombo->setCurrentIndex(ui->effectCombo->findData(layoutName));
}

void KWinTabBoxConfigForm::setEffectComboModel(QStandardItemModel *model)
{
    int index = ui->effectCombo->currentIndex();
    QVariant data = ui->effectCombo->itemData(index);

    ui->effectCombo->setModel(model);

    if (data.isValid()) {
        ui->effectCombo->setCurrentIndex(ui->effectCombo->findData(data));
    } else if (index != -1) {
        ui->effectCombo->setCurrentIndex(index);
    }
}

QVariant KWinTabBoxConfigForm::effectComboCurrentData(int role) const
{
    return ui->effectCombo->currentData(role);
}

void KWinTabBoxConfigForm::loadShortcuts()
{
    for (const auto &widget : {ui->scAll, ui->scAllReverse, ui->scCurrent, ui->scCurrentReverse}) {
        const QString actionName = widget->property("shortcutAction").toString();
        const auto shortcuts = KGlobalAccel::self()->globalShortcut(QStringLiteral("kwin"), actionName);
        if (!shortcuts.isEmpty()) {
            widget->setKeySequence(shortcuts.first());
        }
    }
}

void KWinTabBoxConfigForm::resetShortcuts()
{
    for (auto const& widget : {ui->scAll, ui->scAllReverse, ui->scCurrent, ui->scCurrentReverse}) {
        const QString actionName = widget->property("shortcutAction").toString();
        QAction *action = m_actionCollection->action(actionName);
        widget->setKeySequence(m_actionCollection->defaultShortcut(action));
    }
}

void KWinTabBoxConfigForm::saveShortcuts()
{
    for (const auto &widget : {ui->scAll, ui->scAllReverse, ui->scCurrent, ui->scCurrentReverse}) {
        const QString actionName = widget->property("shortcutAction").toString();
        QAction *action = m_actionCollection->action(actionName);
        KGlobalAccel::self()->setShortcut(action, {action->shortcut()}, KGlobalAccel::NoAutoloading);
    }
}

bool KWinTabBoxConfigForm::isShortcutsChanged() const
{
    for (const auto &widget : {ui->scAll, ui->scAllReverse, ui->scCurrent, ui->scCurrentReverse}) {
        const QString actionName = widget->property("shortcutAction").toString();
        QAction *action = m_actionCollection->action(actionName);
        const auto shortcuts = KGlobalAccel::self()->globalShortcut(QStringLiteral("kwin"), actionName);
        const QKeySequence savedShortcut = shortcuts.isEmpty() ? QKeySequence() : shortcuts.first();

        if (action->shortcut() != savedShortcut) {
            return true;
        }
    }
    return false;
}

bool KWinTabBoxConfigForm::isShortcutsDefault() const
{
    for (const auto &widget : {ui->scAll, ui->scAllReverse, ui->scCurrent, ui->scCurrentReverse}) {
        const QString actionName = widget->property("shortcutAction").toString();
        QAction *action = m_actionCollection->action(actionName);
        if (action->shortcut() != m_actionCollection->defaultShortcut(action)) {
            return false;
        }
    }
    return true;
}

void KWinTabBoxConfigForm::setHighlightWindowsEnabled(bool enabled)
{
    m_isHighlightWindowsEnabled = enabled;
    ui->kcfg_HighlightWindows->setEnabled(m_isHighlightWindowsEnabled);
}

void KWinTabBoxConfigForm::setFilterScreenEnabled(bool enabled)
{
    ui->filterScreens->setEnabled(enabled);
    ui->currentScreen->setEnabled(enabled);
    ui->otherScreens->setEnabled(enabled);
}

void KWinTabBoxConfigForm::setFilterDesktopEnabled(bool enabled)
{
    ui->filterDesktops->setEnabled(enabled);
    ui->currentDesktop->setEnabled(enabled);
    ui->otherDesktops->setEnabled(enabled);
}

void KWinTabBoxConfigForm::setFilterMinimizationEnabled(bool enabled)
{
    ui->filterMinimization->setEnabled(enabled);
    ui->visibleWindows->setEnabled(enabled);
    ui->hiddenWindows->setEnabled(enabled);
}

void KWinTabBoxConfigForm::setApplicationModeEnabled(bool enabled)
{
    ui->oneAppWindow->setEnabled(enabled);
}

void KWinTabBoxConfigForm::setShowDesktopModeEnabled(bool enabled)
{
    ui->showDesktop->setEnabled(enabled);
}

void KWinTabBoxConfigForm::setSwitchingModeEnabled(bool enabled)
{
    ui->switchingModeCombo->setEnabled(enabled);
}

void KWinTabBoxConfigForm::setLayoutNameEnabled(bool enabled)
{
    ui->effectCombo->setEnabled(enabled);
}

void KWinTabBoxConfigForm::setFilterScreenDefaultIndicatorVisible(bool visible)
{
    setDefaultIndicatorVisible(ui->filterScreens, visible);
    setDefaultIndicatorVisible(ui->currentScreen, visible);
    setDefaultIndicatorVisible(ui->otherScreens, visible);
}

void KWinTabBoxConfigForm::setFilterDesktopDefaultIndicatorVisible(bool visible)
{
    setDefaultIndicatorVisible(ui->filterDesktops, visible);
    setDefaultIndicatorVisible(ui->currentDesktop, visible);
    setDefaultIndicatorVisible(ui->otherDesktops, visible);
}

void KWinTabBoxConfigForm::setFilterMinimizationDefaultIndicatorVisible(bool visible)
{
    setDefaultIndicatorVisible(ui->filterMinimization, visible);
    setDefaultIndicatorVisible(ui->visibleWindows, visible);
    setDefaultIndicatorVisible(ui->hiddenWindows, visible);
}

void KWinTabBoxConfigForm::setApplicationModeDefaultIndicatorVisible(bool visible)
{
    setDefaultIndicatorVisible(ui->oneAppWindow, visible);
}

void KWinTabBoxConfigForm::setShowDesktopModeDefaultIndicatorVisible(bool visible)
{
    setDefaultIndicatorVisible(ui->showDesktop, visible);
}

void KWinTabBoxConfigForm::setSwitchingModeDefaultIndicatorVisible(bool visible)
{
    setDefaultIndicatorVisible(ui->switchingModeCombo, visible);
}

void KWinTabBoxConfigForm::setLayoutNameDefaultIndicatorVisible(bool visible)
{
    setDefaultIndicatorVisible(ui->effectCombo, visible);
}

void KWinTabBoxConfigForm::setShortcutsDefaultIndicatorVisible(bool visible)
{
    for (const auto &widget : {ui->scAll, ui->scAllReverse, ui->scCurrent, ui->scCurrentReverse}) {
        const QString actionName = widget->property("shortcutAction").toString();
        QAction *action = m_actionCollection->action(actionName);
        const bool isDefault = (action->shortcut() == m_actionCollection->defaultShortcut(action));

        setDefaultIndicatorVisible(widget, visible && !isDefault);
    }
}

void KWinTabBoxConfigForm::tabBoxToggled(bool on)
{
    // Highlight Windows options is availabled if no TabBox effect is selected
    // or if Tabbox is not builtin effet.
    on = !on || ui->effectCombo->currentData(AddonEffect).toBool();
    ui->kcfg_HighlightWindows->setEnabled(on && m_isHighlightWindowsEnabled);
}

void KWinTabBoxConfigForm::onFilterScreen()
{
    Q_EMIT filterScreenChanged(filterScreen());
}

void KWinTabBoxConfigForm::onFilterDesktop()
{
    Q_EMIT filterDesktopChanged(filterDesktop());
}

void KWinTabBoxConfigForm::onFilterMinimization()
{
    Q_EMIT filterMinimizationChanged(filterMinimization());
}

void KWin::KWinTabBoxConfigForm::onApplicationMode()
{
    Q_EMIT applicationModeChanged(applicationMode());
}

void KWinTabBoxConfigForm::onShowDesktopMode()
{
    Q_EMIT showDesktopModeChanged(showDesktopMode());
}

void KWinTabBoxConfigForm::onSwitchingMode()
{
    Q_EMIT switchingModeChanged(switchingMode());
}

void KWinTabBoxConfigForm::onEffectCombo()
{
    const bool isAddonEffect = ui->effectCombo->currentData(AddonEffect).toBool();
    ui->effectConfigButton->setIcon(QIcon::fromTheme(isAddonEffect ? "view-preview" : "configure"));
    if (!ui->kcfg_ShowTabBox->isChecked()) {
        return;
    }
    ui->kcfg_HighlightWindows->setEnabled(isAddonEffect && m_isHighlightWindowsEnabled);

    Q_EMIT layoutNameChanged(layoutName());
}

void KWinTabBoxConfigForm::onShortcutChanged(const QKeySequence &seq)
{
    QString actionName;
    if (sender()) {
        actionName = sender()->property("shortcutAction").toString();
    }
    if (actionName.isEmpty()) {
        return;
    }
    QAction *action = m_actionCollection->action(actionName);
    action->setShortcut(seq);

    Q_EMIT shortcutChanged();
}

void KWinTabBoxConfigForm::setDefaultIndicatorVisible(QWidget *widget, bool visible)
{
    widget->setProperty("_kde_highlight_neutral", visible);
    widget->update();
}

} // namespace
