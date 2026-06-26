/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "layoutengine.h"

namespace KWin
{

LayoutEngine::LayoutEngine(QObject *parent)
    : QObject(parent)
{
}

LayoutEngine::~LayoutEngine() = default;

QString LayoutEngine::layoutKindToString(LayoutKind kind)
{
    switch (kind) {
    case LayoutKind::MasterStack:
        return QStringLiteral("MasterStack");
    case LayoutKind::Stacked:
        return QStringLiteral("Stacked");
    case LayoutKind::CenterTile:
        return QStringLiteral("CenterTile");
    }
    return QStringLiteral("MasterStack");
}

LayoutEngine::LayoutKind LayoutEngine::layoutKindFromString(const QString &name, LayoutKind fallback)
{
    if (name.compare(QLatin1String("MasterStack"), Qt::CaseInsensitive) == 0) {
        return LayoutKind::MasterStack;
    }
    if (name.compare(QLatin1String("Stacked"), Qt::CaseInsensitive) == 0) {
        return LayoutKind::Stacked;
    }
    if (name.compare(QLatin1String("CenterTile"), Qt::CaseInsensitive) == 0) {
        return LayoutKind::CenterTile;
    }
    return fallback;
}

} // namespace KWin
