/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwin_export.h"
#include "tilingstate.h"

#include <QStringList>

class KConfigGroup;

namespace KWin
{

class Window;

/**
 * Decides whether a window should be tiled or floated based on configured rules
 * and window properties.
 */
class KWIN_EXPORT TilingRules
{
public:
    explicit TilingRules();
    explicit TilingRules(const KConfigGroup &group);

    void load(const KConfigGroup &group);

    /**
     * Returns true if the window should be ignored entirely by tiling
     * (e.g. shell/panel/launcher windows).
     */
    bool isIgnored(const Window *window) const;

    /**
     * Returns true if the window should be forced to float.
     */
    bool isFloating(const Window *window) const;

    /**
     * Returns the initial tiling mode for a newly created window.
     */
    TilingState::Mode initialMode(const Window *window) const;

    bool floatUtility() const { return m_floatUtility; }
    bool floatDialog() const { return m_floatDialog; }
    bool floatTransient() const { return m_floatTransient; }

private:
    bool matchClass(const Window *window, const QStringList &patterns) const;
    bool matchTitle(const Window *window, const QStringList &patterns) const;

    QStringList m_ignoreClasses;
    QStringList m_ignoreTitles;
    QStringList m_floatingClasses;
    QStringList m_floatingTitles;
    bool m_floatUtility = true;
    bool m_floatDialog = true;
    bool m_floatTransient = true;
};

} // namespace KWin
