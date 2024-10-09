/*
    SPDX-FileCopyrightText: 2020 Ismael Asensio <isma.af@gmail.com>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

import org.kde.kirigami 2.10 as Kirigami
import org.kde.kquickcontrols as KQC
import org.kde.kcms.kwinrules


Loader {
    id: valueEditor
    focus: true

    property var ruleValue
    property var ruleOptions
    property int controlType

    signal valueEdited(var value)

    sourceComponent: {
        switch (controlType) {
            case RuleItem.Boolean: return booleanEditor
            case RuleItem.String: return stringEditor
            case RuleItem.Integer: return integerEditor
            case RuleItem.Option: return optionEditor
            case RuleItem.NetTypes: return netTypesEditor
            case RuleItem.Percentage: return percentageEditor
            case RuleItem.Point: return coordinateEditor
            case RuleItem.Size: return coordinateEditor
            case RuleItem.Shortcut: return shortcutEditor
            case RuleItem.OptionList: return optionListEditor
            default: return emptyEditor
        }
    }

    Component {
        id: emptyEditor
        Item {}
    }

    Component {
        id: booleanEditor
        RowLayout {
            Item {
                Layout.fillWidth: true
            }
            QQC2.RadioButton {
                text: i18n("Yes")
                checked: ruleValue
                Layout.margins: Kirigami.Units.smallSpacing
                onToggled: valueEditor.valueEdited(checked)
            }
            QQC2.RadioButton {
                text: i18n("No")
                checked: !ruleValue
                Layout.margins: Kirigami.Units.smallSpacing
                onToggled: valueEditor.valueEdited(!checked)
            }
        }
    }

    Component {
        id: stringEditor
        QQC2.TextField {
            id: stringTextField
            property bool isTextEdited: false
            horizontalAlignment: Text.AlignLeft
            onTextEdited: { valueEditor.valueEdited(text); }
            Connections {
                target: valueEditor
                function onRuleValueChanged() {
                    if (!stringTextField.activeFocus) {  // Protects from self-updating when editing
                        stringTextField.text = valueEditor.ruleValue
                    }
                }
            }
            Component.onCompleted: { this.text = valueEditor.ruleValue }
        }
    }

    Component {
        id: integerEditor
        QQC2.SpinBox {
            editable: true
            value: ruleValue
            onValueModified: valueEditor.valueEdited(value)
        }
    }

    Component {
        id: optionEditor
        OptionsComboBox {
            model: ruleOptions
            onActivated: (index) => {
                valueEditor.valueEdited(currentValue);
            }
        }
    }

    Component {
        id: netTypesEditor
        OptionsComboBox {
            model: ruleOptions
            multipleChoice: true
            // Filter the provided value with the options mask
            selectionMask: ruleValue & model.allOptionsMask
            onActivated: {
                valueEditor.valueEdited(selectionMask);
            }
        }
    }

    Component {
        id: optionListEditor
        OptionsComboBox {
            id: optionListCombo
            model: ruleOptions
            multipleChoice: true

            onActivated: {
                const selectionList = []
                for (let i = 0; i < count; i++) {
                    if (selectionMask & (1 << i)) {
                        selectionList.push(model.data(model.index(i,0), Qt.UserRole))
                    }
                }
                valueEditor.valueEdited(selectionList);
            }

            function updateSelectionMask() {
                selectionMask = 0
                for (let i = 0; i < count; i++) {
                    if (ruleValue.includes(model.data(model.index(i,0), Qt.UserRole))) {
                        selectionMask += 1 << i
                    }
                }
            }

            onModelChanged: updateSelectionMask()
            Component.onCompleted: updateSelectionMask()
            Connections {
                target: valueEditor
                function onRuleValueChanged() {
                    optionListCombo.updateSelectionMask()
                }
            }
        }
    }

    Component {
        id: percentageEditor
        RowLayout {
            QQC2.Slider {
                id: slider
                Layout.fillWidth: true
                from: 0
                to: 100
                value: ruleValue
                onMoved: valueEditor.valueEdited(Math.round(slider.value))
            }
            QQC2.Label {
                text: i18n("%1 %", Math.round(slider.value))
                horizontalAlignment: Qt.AlignRight
                Layout.minimumWidth: maxPercentage.width + Kirigami.Units.smallSpacing
                Layout.margins: Kirigami.Units.smallSpacing
            }
            TextMetrics {
                id: maxPercentage
                text: i18n("%1 %", 100)
            }
        }
    }

    Component {
        id: coordinateEditor
        RowLayout {
            id: coordItem
            spacing: Kirigami.Units.smallSpacing

            readonly property bool isSize: controlType == RuleItem.Size
            readonly property var coord: (isSize) ? Qt.size(coordX.value, coordY.value)
                                                  : Qt.point(coordX.value, coordY.value)

            QQC2.SpinBox {
                id: coordX
                editable: true
                Layout.preferredWidth: 50   // 50%
                Layout.fillWidth: true
                from: (isSize) ? 0 : -32767
                to: 32767
                value: (isSize) ? ruleValue.width : ruleValue.x
                onValueModified: valueEditor.valueEdited(coord)
            }
            QQC2.Label {
                id: coordSeparator
                Layout.preferredWidth: implicitWidth
                text: i18nc("(x, y) coordinates separator in size/position","x")
                horizontalAlignment: Text.AlignHCenter
            }
            QQC2.SpinBox {
                id: coordY
                editable: true
                from: coordX.from
                to: coordX.to
                Layout.preferredWidth: 50   // 50%
                Layout.fillWidth: true
                value: (isSize) ? ruleValue.height : ruleValue.y
                onValueModified: valueEditor.valueEdited(coord)
            }
        }
    }

    Component {
        id: shortcutEditor
        RowLayout {
            Item {
                Layout.fillWidth: true
            }
            KQC.KeySequenceItem {
                keySequence: ruleValue
                onCaptureFinished: valueEditor.valueEdited(keySequence)
            }
        }
    }
}
