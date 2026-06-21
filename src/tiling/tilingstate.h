/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwin_export.h"

#include <QColor>

namespace KWin
{

class KWIN_EXPORT TilingState
{
public:
    enum class Mode {
        Floating,
        Tiled,
        Fullscreen,
    };

    enum class FloatReason {
        User,
        Dialog,
        Transient,
        Utility,
        Rule,
        Unresizable,
    };

    Mode mode = Mode::Floating;
    FloatReason floatReason = FloatReason::User;

    /**
     * The window's position in the current layout order (e.g. master-stack index).
     * -1 means the window has not been assigned a layout position.
     */
    int layoutIndex = -1;

    /**
     * True if the user explicitly toggled floating for this window.
     * Used to remember user choice across layout changes.
     */
    bool userToggledFloat = false;

    /**
     * Whether a native tiling border should be drawn around this window.
     */
    bool showBorder = false;

    /**
     * Thickness of the tiling border (in logical pixels).
     */
    qreal borderThickness = 0;

    /**
     * Color of the tiling border.
     */
    QColor borderColor;
};

} // namespace KWin
