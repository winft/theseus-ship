/*
 * Copyright (c) 2020 Ismael Asensio <isma.af@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import QtQuick 2.14
import QtQuick.Layouts 1.14
import QtQuick.Controls 2.14 as QQC2

import org.kde.kirigami 2.10 as Kirigami
import org.kde.kcms.kwinrules 1.0


QQC2.ComboBox {
    id: optionsCombo

    textRole: "display"
    valueRole: "value"

    property bool multipleChoice: false
    property int selectionMask: 0
    readonly property int allOptionsMask: model.allOptionsMask

    currentIndex: multipleChoice ? -1 : model.selectedIndex

    displayText: {
        if (!multipleChoice) {
            return currentText;
        }
        const selectionCount = selectionMask.toString(2).replace(/0/g, '').length;
        const optionsCount = allOptionsMask.toString(2).replace(/0/g, '').length;
        switch (selectionCount) {
            case 0:
                return i18n("None selected");
            case 1:
                const selectedBit = selectionMask.toString(2).length - 1;
                const selectedIndex = (model.useFlags) ? model.indexOf(selectionMask) : selectedBit
                return model.data(model.index(selectedIndex, 0), Qt.DisplayRole);
            case optionsCount:
                return i18n("All selected");
        }
        return i18np("%1 selected", "%1 selected", selectionCount);
    }

    delegate: QQC2.ItemDelegate {
        id: delegateItem

        highlighted: optionsCombo.highlightedIndex == index
        width: parent.width

        contentItem: RowLayout {
            QQC2.RadioButton {
                id: radioButton
                visible: multipleChoice && model.optionType === OptionsModel.ExclusiveOption
                checked: (selectionMask & bitMask) == bitMask
                enabled: false  // We don't want to uncheck the exclusive option on toggle
            }
            QQC2.CheckBox {
                id: checkBox
                visible: multipleChoice && model.optionType !== OptionsModel.ExclusiveOption
                checked: (selectionMask & model.bitMask) == model.bitMask
                onToggled: {
                    selectionMask = (checked) ? selectionMask | model.bitMask : selectionMask & ~model.bitMask;
                    selectionMask &= allOptionsMask;
                    activated(index);
                }
            }
            Kirigami.Icon {
                source: model.decoration
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
            }
            QQC2.Label {
                text: model.display
                color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
            }
        }

        MouseArea {
            anchors.fill: contentItem
            enabled: multipleChoice
            onClicked: {
                if (checkBox.visible) {
                    checkBox.toggle();
                    checkBox.toggled();
                } else if (radioButton.visible) {
                    selectionMask = model.bitMask; // Only check the exclusive option
                    activated(index);
                }
            }
        }

        QQC2.ToolTip {
            text: model.tooltip
            visible: hovered && (model.tooltip.length > 0)
        }

        Component.onCompleted: {
            //FIXME: work around bug https://bugs.kde.org/show_bug.cgi?id=403153
            optionsCombo.popup.width = Math.max(implicitWidth, optionsCombo.width, optionsCombo.popup.width);
        }

        onActiveFocusChanged: {
            if (!activeFocus) {
                popup.close();
            }
        }
    }
}
