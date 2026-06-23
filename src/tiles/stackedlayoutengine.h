/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "layoutengine.h"
#include "tile.h"

#include <QList>
#include <QPointer>

namespace KWin
{

class CustomTile;
class RootTile;
class Window;

/**
 * Stacked (single-column) layout.
 *
 * Every tiled window fills the full width of the root and is stacked vertically.
 * The first window in the list occupies the top strip; subsequent windows fill
 * the remaining height evenly. There is no separate master / primary window.
 *
 * Designed primarily for vertical (portrait) monitors, but works on any aspect
 * ratio. New windows are appended to the bottom of the stack.
 */
class KWIN_EXPORT StackedLayoutEngine : public LayoutEngine
{
    Q_OBJECT

public:
    explicit StackedLayoutEngine(QObject *parent = nullptr);
    ~StackedLayoutEngine() override;

    LayoutKind layoutKind() const override { return LayoutKind::Stacked; }
    void attach(RootTile *root) override;
    void addWindow(Window *window) override;
    void removeWindow(Window *window) override;
    void moveWindow(Window *window, int delta) override;
    void beginMoveWindow(Window *window) override;
    bool endMoveWindow(Window *window, Window *target) override;
    void cancelMoveWindow(Window *window) override;
    void reflow() override;

    QList<Window *> windows() const override;
    Window *primaryWindow() const override;
    Window *windowInDirection(Window *from, FocusDirection direction) const override;

    bool supportsPerTileResize() const override { return true; }
    void adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis) override;
    void adjustColumnRatio(qreal delta) override { Q_UNUSED(delta) }

private:
    int indexOfWindow(Window *window) const;

    QPointer<RootTile> m_root;
    QList<QPointer<CustomTile>> m_leaves;
    // Per-tile weight parallel to m_leaves. Each new leaf starts at 1.0;
    // reflow() distributes the column height proportionally. See
    // MasterStackLayoutEngine for the floor/renormalisation details.
    QList<qreal> m_weights;

    // Source leaf remembered during an interactive drag-move so the window can
    // be swapped with another tiled window or restored to its original place.
    QPointer<CustomTile> m_moveSourceLeaf;
    int m_moveSourceIndex = -1;
};

} // namespace KWin
