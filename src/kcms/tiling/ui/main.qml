/*
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM
import org.kde.kquickcontrols as KQC
import org.kde.kwin.kcm.tiling

KCM.SimpleKCM {
    id: root

    implicitWidth: Kirigami.Units.gridUnit * 40
    implicitHeight: Kirigami.Units.gridUnit * 30

    header: QQC2.TabBar {
        id: tabBar

        QQC2.TabButton {
            text: i18nc("@title:tab", "Layouts")
        }
        QQC2.TabButton {
            text: i18nc("@title:tab", "Gaps & Per-Monitor")
        }
        QQC2.TabButton {
            text: i18nc("@title:tab", "Window Borders")
        }
        QQC2.TabButton {
            text: i18nc("@title:tab", "Floating Rules")
        }
    }

    StackLayout {
        id: stackLayout
        anchors.fill: parent
        currentIndex: tabBar.currentIndex

        Kirigami.FormLayout {
            id: layoutsPage

            QQC2.CheckBox {
                id: enableTiling
                Kirigami.FormData.label: i18n("Tiling:")
                text: i18n("Enable tiling")
                checked: kcm.settings.enabled
                onToggled: kcm.settings.enabled = checked
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "enabled"
                }
            }

            Item {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Layouts")
            }

            QQC2.CheckBox {
                id: enableMasterStack
                Kirigami.FormData.label: i18n("Available:")
                text: i18n("MasterStack")
                checked: kcm.settings.enabledLayouts.indexOf("MasterStack") !== -1
                onToggled: {
                    let layouts = kcm.settings.enabledLayouts.slice();
                    if (checked && layouts.indexOf("MasterStack") === -1) {
                        layouts.push("MasterStack");
                    } else if (!checked) {
                        layouts = layouts.filter(l => l !== "MasterStack");
                    }
                    kcm.settings.enabledLayouts = layouts;
                }
            }

            QQC2.CheckBox {
                id: enableStacked
                text: i18n("Stacked")
                checked: kcm.settings.enabledLayouts.indexOf("Stacked") !== -1
                onToggled: {
                    let layouts = kcm.settings.enabledLayouts.slice();
                    if (checked && layouts.indexOf("Stacked") === -1) {
                        layouts.push("Stacked");
                    } else if (!checked) {
                        layouts = layouts.filter(l => l !== "Stacked");
                    }
                    kcm.settings.enabledLayouts = layouts;
                }
            }

            QQC2.CheckBox {
                id: enableCenterTile
                text: i18n("CenterTile")
                checked: kcm.settings.enabledLayouts.indexOf("CenterTile") !== -1
                onToggled: {
                    let layouts = kcm.settings.enabledLayouts.slice();
                    if (checked && layouts.indexOf("CenterTile") === -1) {
                        layouts.push("CenterTile");
                    } else if (!checked) {
                        layouts = layouts.filter(l => l !== "CenterTile");
                    }
                    kcm.settings.enabledLayouts = layouts;
                }
            }

            QQC2.Label {
                visible: kcm.settings.enabledLayouts.length < 2
                Kirigami.FormData.label: i18nc("@info", "Note:")
                text: i18nc("@info", "Enable at least one layout. If none are enabled, the global default below is used.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                opacity: 0.7
            }

            QQC2.ComboBox {
                id: defaultLayout
                Kirigami.FormData.label: i18n("Default layout:")
                model: [
                    { text: i18n("MasterStack"), value: "MasterStack" },
                    { text: i18n("Stacked"), value: "Stacked" },
                    { text: i18n("CenterTile"), value: "CenterTile" }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: {
                    const idx = model.findIndex(item => item.value === kcm.settings.defaultLayout);
                    return idx >= 0 ? idx : 0;
                }
                onActivated: kcm.settings.defaultLayout = currentValue
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "defaultLayout"
                }
            }

            Item {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Resizing")
            }

            QQC2.SpinBox {
                id: resizeStep
                Kirigami.FormData.label: i18n("Resize step (%):")
                from: 1
                to: 100
                value: Math.round(kcm.settings.resizeStep * 100)
                onValueModified: kcm.settings.resizeStep = value / 100.0
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "resizeStep"
                }
            }

            QQC2.Label {
                Kirigami.FormData.label: i18nc("@info", "Note:")
                text: xi18nc("@info", "How much each <interface>Window Grow/Shrink Horizontal/Vertical</interface> shortcut press changes a tiled window's share of its column. The KDE-standard <shortcut>Meta+Plus</shortcut> and <shortcut>Meta+Shift+Plus</shortcut> shortcuts are reused; the active window grows and its column-mates shrink to keep the layout balanced.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                opacity: 0.7
            }
        }

        Kirigami.FormLayout {
            id: gapsPage

            Item {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Gaps")
            }

            QQC2.SpinBox {
                id: gapLeft
                Kirigami.FormData.label: i18n("Left:")
                from: 0
                to: 1000
                value: kcm.settings.gapLeft
                onValueModified: kcm.settings.gapLeft = value
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "gapLeft"
                }
            }

            QQC2.SpinBox {
                id: gapRight
                Kirigami.FormData.label: i18n("Right:")
                from: 0
                to: 1000
                value: kcm.settings.gapRight
                onValueModified: kcm.settings.gapRight = value
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "gapRight"
                }
            }

            QQC2.SpinBox {
                id: gapTop
                Kirigami.FormData.label: i18n("Top:")
                from: 0
                to: 1000
                value: kcm.settings.gapTop
                onValueModified: kcm.settings.gapTop = value
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "gapTop"
                }
            }

            QQC2.SpinBox {
                id: gapBottom
                Kirigami.FormData.label: i18n("Bottom:")
                from: 0
                to: 1000
                value: kcm.settings.gapBottom
                onValueModified: kcm.settings.gapBottom = value
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "gapBottom"
                }
            }

            QQC2.SpinBox {
                id: gapBetween
                Kirigami.FormData.label: i18n("Between tiles:")
                from: 0
                to: 1000
                value: kcm.settings.gapBetween
                onValueModified: kcm.settings.gapBetween = value
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "gapBetween"
                }
            }

            Item {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Per-Monitor Settings")
            }

            QQC2.Label {
                id: perMonitorHelp
                Kirigami.FormData.label: i18nc("@info:placeholder", "Overrides:")
                text: i18nc("@info", "Per-monitor gap and layout values override the defaults above. Monitors without an override use the defaults. Leave the values as-is to use defaults.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 30
            }

            Repeater {
                id: perMonitorRepeater
                model: kcm.gapOverridesModel

                delegate: QQC2.Frame {
                    id: monitorFrame
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing

                    // Pull the Repeater model data into local properties.  Inside
                    // child controls such as ComboBox the name "model" resolves to
                    // the control's own model property, so we must not reach back
                    // through `model.entry` there.
                    property var entry: model.entry
                    property string monitorName: model.name
                    property string monitorDescription: model.description

                    background: Rectangle {
                        color: "transparent"
                        border.color: Kirigami.Theme.textColor
                        border.width: 1
                        radius: Kirigami.Units.smallSpacing
                        opacity: 0.3
                    }

                    contentItem: ColumnLayout {
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            text: monitorFrame.monitorDescription + (monitorFrame.monitorName ? "  (" + monitorFrame.monitorName + ")" : "")
                            font.bold: true
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: Kirigami.Units.largeSpacing
                            rowSpacing: Kirigami.Units.smallSpacing
                            Layout.fillWidth: true

                            QQC2.Label {
                                text: i18n("Default layout:")
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                            QQC2.ComboBox {
                                id: perMonitorLayoutCombo
                                property var layoutOptions: [
                                    { text: i18n("MasterStack"), value: "MasterStack" },
                                    { text: i18n("Stacked"), value: "Stacked" },
                                    { text: i18n("CenterTile"), value: "CenterTile" }
                                ]
                                model: layoutOptions
                                textRole: "text"
                                valueRole: "value"
                                currentIndex: {
                                    const cur = monitorFrame.entry ? monitorFrame.entry.defaultLayout : "MasterStack";
                                    const idx = layoutOptions.findIndex(item => item.value === cur);
                                    return idx >= 0 ? idx : 0;
                                }
                                onActivated: {
                                    if (monitorFrame.entry) {
                                        monitorFrame.entry.defaultLayout = layoutOptions[currentIndex].value;
                                    }
                                }
                            }

                            QQC2.Label {
                                text: i18n("Left gap:")
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                            QQC2.SpinBox {
                                from: 0
                                to: 1000
                                value: monitorFrame.entry ? monitorFrame.entry.gapLeft : 0
                                onValueModified: if (monitorFrame.entry) monitorFrame.entry.gapLeft = value
                            }

                            QQC2.Label {
                                text: i18n("Right gap:")
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                            QQC2.SpinBox {
                                from: 0
                                to: 1000
                                value: monitorFrame.entry ? monitorFrame.entry.gapRight : 0
                                onValueModified: if (monitorFrame.entry) monitorFrame.entry.gapRight = value
                            }

                            QQC2.Label {
                                text: i18n("Top gap:")
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                            QQC2.SpinBox {
                                from: 0
                                to: 1000
                                value: monitorFrame.entry ? monitorFrame.entry.gapTop : 0
                                onValueModified: if (monitorFrame.entry) monitorFrame.entry.gapTop = value
                            }

                            QQC2.Label {
                                text: i18n("Bottom gap:")
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                            QQC2.SpinBox {
                                from: 0
                                to: 1000
                                value: monitorFrame.entry ? monitorFrame.entry.gapBottom : 0
                                onValueModified: if (monitorFrame.entry) monitorFrame.entry.gapBottom = value
                            }

                            QQC2.Label {
                                text: i18n("Between tiles gap:")
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                            QQC2.SpinBox {
                                from: 0
                                to: 1000
                                value: monitorFrame.entry ? monitorFrame.entry.gapBetween : 0
                                onValueModified: if (monitorFrame.entry) monitorFrame.entry.gapBetween = value
                            }
                        }
                    }
                }
            }

            QQC2.Label {
                id: noMonitorsLabel
                visible: kcm.gapOverridesModel.count === 0
                Kirigami.FormData.label: i18nc("@info", "Status:")
                text: i18nc("@info", "No connected monitors detected. Per-monitor overrides will appear here once a monitor is connected.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 30
            }
        }

        Kirigami.FormLayout {
            id: bordersPage

            QQC2.ComboBox {
                id: tilingBorderMode
                Kirigami.FormData.label: i18n("Border mode:")
                model: [
                    i18n("None"),
                    i18n("All tiled windows"),
                    i18n("Active window only"),
                    i18n("All windows")
                ]
                currentIndex: kcm.settings.tilingBorderMode
                onActivated: kcm.settings.tilingBorderMode = currentIndex
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "tilingBorderMode"
                }
            }

            QQC2.SpinBox {
                id: tilingBorderThickness
                Kirigami.FormData.label: i18n("Border thickness:")
                from: 0
                to: 20
                value: kcm.settings.tilingBorderThickness
                onValueModified: kcm.settings.tilingBorderThickness = value
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "tilingBorderThickness"
                }
            }

            QQC2.ComboBox {
                id: tilingBorderColorSourceActive
                Kirigami.FormData.label: i18n("Active border source:")
                model: [
                    i18n("Custom color"),
                    i18n("System accent"),
                    i18n("Noctalia primary"),
                    i18n("Noctalia accent")
                ]
                currentIndex: kcm.settings.tilingBorderColorSourceActive
                onActivated: kcm.settings.tilingBorderColorSourceActive = currentIndex
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "tilingBorderColorSourceActive"
                }
            }

            KQC.ColorButton {
                id: tilingBorderColorActive
                Kirigami.FormData.label: i18n("Active border color:")
                visible: kcm.settings.tilingBorderColorSourceActive === 0
                color: kcm.settings.tilingBorderColorActive
                showAlphaChannel: true
                dialogTitle: i18n("Choose active border color")
                onAccepted: kcm.settings.tilingBorderColorActive = color
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "tilingBorderColorActive"
                }
            }

            QQC2.Label {
                visible: kcm.settings.tilingBorderColorSourceActive !== 0
                Kirigami.FormData.label: i18n("Active color preview:")
                text: {
                    switch (kcm.settings.tilingBorderColorSourceActive) {
                    case 1:
                        return i18nc("@info", "Follows the system accent color.");
                    case 2:
                        return i18nc("@info", "Follows the Noctalia primary color.");
                    case 3:
                        return i18nc("@info", "Follows the Noctalia accent color.");
                    default:
                        return "";
                    }
                }
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                opacity: 0.7
            }

            QQC2.ComboBox {
                id: tilingBorderColorSourceInactive
                Kirigami.FormData.label: i18n("Inactive border source:")
                model: [
                    i18n("Custom color"),
                    i18n("System accent"),
                    i18n("System accent (faded)"),
                    i18n("Noctalia primary"),
                    i18n("Noctalia accent")
                ]
                currentIndex: kcm.settings.tilingBorderColorSourceInactive
                onActivated: kcm.settings.tilingBorderColorSourceInactive = currentIndex
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "tilingBorderColorSourceInactive"
                }
            }

            KQC.ColorButton {
                id: tilingBorderColorInactive
                Kirigami.FormData.label: i18n("Inactive border color:")
                visible: kcm.settings.tilingBorderColorSourceInactive === 0
                color: kcm.settings.tilingBorderColorInactive
                showAlphaChannel: true
                dialogTitle: i18n("Choose inactive border color")
                onAccepted: kcm.settings.tilingBorderColorInactive = color
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "tilingBorderColorInactive"
                }
            }

            QQC2.Label {
                visible: kcm.settings.tilingBorderColorSourceInactive !== 0
                Kirigami.FormData.label: i18n("Inactive color preview:")
                text: {
                    switch (kcm.settings.tilingBorderColorSourceInactive) {
                    case 1:
                        return i18nc("@info", "Follows the system accent color.");
                    case 2:
                        return i18nc("@info", "System accent at 50% alpha.");
                    case 3:
                        return i18nc("@info", "Follows the Noctalia primary color.");
                    case 4:
                        return i18nc("@info", "Follows the Noctalia accent color.");
                    default:
                        return "";
                    }
                }
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                opacity: 0.7
            }

            QQC2.SpinBox {
                id: tilingCornerRadius
                Kirigami.FormData.label: i18n("Corner radius:")
                from: 0
                to: 50
                value: kcm.settings.tilingCornerRadius
                onValueModified: kcm.settings.tilingCornerRadius = value
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "tilingCornerRadius"
                }
            }
        }

        Kirigami.FormLayout {
            id: floatingPage

            QQC2.TextField {
                id: floatingClasses
                Kirigami.FormData.label: i18n("Floating classes:")
                text: kcm.settings.floatingClasses.join(", ")
                onEditingFinished: kcm.settings.floatingClasses = text.split(",").map(s => s.trim()).filter(s => s.length > 0)
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "floatingClasses"
                }
            }

            QQC2.CheckBox {
                id: floatUtility
                Kirigami.FormData.label: i18n("Window types:")
                text: i18n("Float utility windows")
                checked: kcm.settings.floatUtility
                onToggled: kcm.settings.floatUtility = checked
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "floatUtility"
                }
            }

            QQC2.CheckBox {
                id: floatDialog
                text: i18n("Float dialog windows")
                checked: kcm.settings.floatDialog
                onToggled: kcm.settings.floatDialog = checked
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "floatDialog"
                }
            }

            QQC2.CheckBox {
                id: floatTransient
                text: i18n("Float transient windows")
                checked: kcm.settings.floatTransient
                onToggled: kcm.settings.floatTransient = checked
                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "floatTransient"
                }
            }
        }
    }
}
