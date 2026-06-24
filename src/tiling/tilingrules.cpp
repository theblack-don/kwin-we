/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tilingrules.h"
#include "window.h"

#include <KConfigGroup>

namespace KWin
{

TilingRules::TilingRules()
{
}

TilingRules::TilingRules(const KConfigGroup &group)
{
    load(group);
}

void TilingRules::load(const KConfigGroup &group)
{
    m_ignoreClasses = group.readEntry("IgnoreClass", QStringList());
    m_ignoreTitles = group.readEntry("IgnoreTitle", QStringList());
    m_floatingClasses = group.readEntry("FloatingClass", QStringList());
    m_floatingTitles = group.readEntry("FloatingTitle", QStringList());
    m_floatUtility = group.readEntry("FloatUtility", true);
    m_floatDialog = group.readEntry("FloatDialog", true);
    m_floatTransient = group.readEntry("FloatTransient", true);

    // Normalize class patterns: strip whitespace and lower-case for case-insensitive matching.
    auto normalize = [](QStringList &list) {
        for (QString &s : list) {
            s = s.trimmed().toLower();
        }
    };
    normalize(m_ignoreClasses);
    normalize(m_ignoreTitles);
    normalize(m_floatingClasses);
    normalize(m_floatingTitles);
}

bool TilingRules::isIgnored(const Window *window) const
{
    if (!window || !window->isClient()) {
        return true;
    }
    if (matchClass(window, m_ignoreClasses)) {
        return true;
    }
    if (matchTitle(window, m_ignoreTitles)) {
        return true;
    }
    return false;
}

bool TilingRules::isFloating(const Window *window) const
{
    if (!window) {
        return true;
    }
    if (!window->isResizable()) {
        return true;
    }
    if (m_floatDialog && window->isDialog()) {
        return true;
    }
    if (m_floatTransient && window->isTransient()) {
        return true;
    }
    if (m_floatUtility && window->isUtility()) {
        return true;
    }
    // On Wayland, dialog and utility windows are represented as transient windows
    // (xdg-toplevel with a parent). If floatDialog or floatUtility is enabled,
    // check transiency as a Wayland-compatible fallback.
    if ((m_floatDialog || m_floatUtility) && window->isTransient()) {
        return true;
    }
    if (window->isPopupWindow() || window->isAppletPopup()) {
        return true;
    }
    if (matchClass(window, m_floatingClasses)) {
        return true;
    }
    if (matchTitle(window, m_floatingTitles)) {
        return true;
    }
    return false;
}

TilingState::Mode TilingRules::initialMode(const Window *window) const
{
    if (isIgnored(window)) {
        return TilingState::Mode::Floating;
    }
    if (isFloating(window)) {
        return TilingState::Mode::Floating;
    }
    return TilingState::Mode::Tiled;
}

bool TilingRules::matchClass(const Window *window, const QStringList &patterns) const
{
    if (patterns.isEmpty()) {
        return false;
    }
    const QString resourceClass = window->resourceClass().toLower();
    const QString resourceName = window->resourceName().toLower();
    const QString desktopFile = window->desktopFileName().toLower();
    for (const QString &pattern : patterns) {
        if (resourceClass.contains(pattern) || resourceName.contains(pattern) || desktopFile.contains(pattern)) {
            return true;
        }
    }
    return false;
}

bool TilingRules::matchTitle(const Window *window, const QStringList &patterns) const
{
    if (patterns.isEmpty()) {
        return false;
    }
    const QString title = window->caption().toLower();
    for (const QString &pattern : patterns) {
        if (title.contains(pattern)) {
            return true;
        }
    }
    return false;
}

} // namespace KWin
