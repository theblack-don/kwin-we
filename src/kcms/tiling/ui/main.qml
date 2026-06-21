/*
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM
import org.kde.kwin.kcm.tiling

KCM.SimpleKCM {
    id: root

    implicitWidth: Kirigami.Units.gridUnit * 40
    implicitHeight: Kirigami.Units.gridUnit * 30

    Kirigami.FormLayout {
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
            Kirigami.FormData.label: i18n("Gaps")
        }

        QQC2.SpinBox {
            id: gapLeft
            Kirigami.FormData.label: i18n("Left:")
            from: 0
            to: 100
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
            to: 100
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
            to: 100
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
            to: 100
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
            to: 100
            value: kcm.settings.gapBetween
            onValueModified: kcm.settings.gapBetween = value
            KCM.SettingStateBinding {
                configObject: kcm.settings
                settingName: "gapBetween"
            }
        }

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Window Borders")
        }

        QQC2.ComboBox {
            id: tilingBorderMode
            Kirigami.FormData.label: i18n("Border mode:")
            model: [
                i18n("None"),
                i18n("All tiled windows"),
                i18n("Active window only")
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

        Item {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Floating Rules")
        }

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
