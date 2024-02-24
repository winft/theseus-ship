/*
    SPDX-FileCopyrightText: 2013 Antonis Tsiapaliokas <kok3rs@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2023 ivan tkachenko <me@ratijas.tk>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami 2 as Kirigami
import org.kde.kcmutils as KCM

QQC2.ItemDelegate {
    id: listItem

    hoverEnabled: true

    onClicked: {
        if (ListView.isCurrentItem) {
            // Collapse list item
            ListView.view.currentIndex = -1;
        } else {
            // Expand list item
            ListView.view.currentIndex = index;
        }
    }

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.RadioButton {
            readonly property bool _exclusive: model.ExclusiveRole != ""
            property bool _toggled: false

            checked: model.StatusRole
            visible: _exclusive
            QQC2.ButtonGroup.group: _exclusive ? effectsList.findButtonGroup(model.ExclusiveRole) : null

            onToggled: {
                model.StatusRole = checked ? Qt.Checked : Qt.Unchecked;
                _toggled = true;
            }

            onClicked: {
                // Uncheck the radio button if it's clicked.
                if (checked && !_toggled) {
                    model.StatusRole = Qt.Unchecked;
                }
                _toggled = false;
            }

            KCM.SettingHighlighter {
                highlight: model.EnabledByDefaultFunctionRole ? parent.checkState !== Qt.PartiallyChecked : parent.checked !== model.EnabledByDefaultRole
            }
        }

        QQC2.CheckBox {
            checkState: model.StatusRole
            visible: model.ExclusiveRole == ""

            onToggled: model.StatusRole = checkState

            KCM.SettingHighlighter {
                highlight: model.EnabledByDefaultFunctionRole ? parent.checkState !== Qt.PartiallyChecked : parent.checked !== model.EnabledByDefaultRole
            }
        }

        ColumnLayout {
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            spacing: 0

            Kirigami.Heading {
                Layout.fillWidth: true

                level: 5
                text: model.NameRole
                wrapMode: Text.Wrap
            }

            QQC2.Label {
                Layout.fillWidth: true

                text: model.DescriptionRole
                opacity: listItem.hovered ? 0.8 : 0.6
                wrapMode: Text.Wrap
            }

            QQC2.Label {
                id: aboutItem

                Layout.fillWidth: true

                text: i18n("Author: %1\nLicense: %2", model.AuthorNameRole, model.LicenseRole)
                opacity: listItem.hovered ? 0.8 : 0.6
                visible: listItem.ListView.isCurrentItem
                wrapMode: Text.Wrap
            }

            Loader {
                id: videoItem

                active: false
                source: "Video.qml"
                visible: false

                function showHide() {
                    if (!videoItem.active) {
                        videoItem.active = true;
                        videoItem.visible = true;
                    } else {
                        videoItem.active = false;
                        videoItem.visible = false;
                    }
                }
            }
        }

        QQC2.ToolButton {
            visible: model.VideoRole.toString() !== ""
            icon.name: "videoclip-amarok"
            text: i18nc("@info:tooltip", "Show/Hide Video")
            display: QQC2.AbstractButton.IconOnly
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            onClicked: videoItem.showHide()
        }

        QQC2.ToolButton  {
            visible: model.ConfigurableRole
            enabled: model.StatusRole != Qt.Unchecked
            icon.name: "configure"
            text: i18nc("@info:tooltip", "Configure…")
            display: QQC2.AbstractButton.IconOnly
            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            onClicked: kcm.configure(model.ServiceNameRole, listItem)
        }
    }
}
