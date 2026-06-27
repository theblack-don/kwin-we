/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "layoutengine.h"

#include <algorithm>

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

void LayoutEngine::interactiveResizeEnded(Window *window, const RectF &before, const RectF &after,
                                          Qt::Edge edge, const QSizeF &outputSize)
{
    if (!window || before.size() == QSizeF() || after.size() == QSizeF()
        || outputSize.width() <= 0 || outputSize.height() <= 0) {
        return;
    }

    qreal delta = 0.0;
    Qt::Orientation axis = Qt::Vertical;
    switch (edge) {
    case Qt::LeftEdge:
        delta = (before.left() - after.left()) / outputSize.width();
        axis = Qt::Horizontal;
        break;
    case Qt::RightEdge:
        delta = (after.right() - before.right()) / outputSize.width();
        axis = Qt::Horizontal;
        break;
    case Qt::TopEdge:
        delta = (before.top() - after.top()) / outputSize.height();
        axis = Qt::Vertical;
        break;
    case Qt::BottomEdge:
        delta = (after.bottom() - before.bottom()) / outputSize.height();
        axis = Qt::Vertical;
        break;
    }
    if (qFuzzyIsNull(delta)) {
        return;
    }
    delta = std::clamp(delta, qreal(-0.5), qreal(0.5));
    adjustTileSize(window, delta, axis);
}

} // namespace KWin
