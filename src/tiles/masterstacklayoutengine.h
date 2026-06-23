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
 * Master-stack layout.
 *
 * The first window is the master and occupies the left portion of the screen.
 * Subsequent windows form a vertical stack on the right.
 */
class KWIN_EXPORT MasterStackLayoutEngine : public LayoutEngine
{
    Q_OBJECT

public:
    explicit MasterStackLayoutEngine(QObject *parent = nullptr);
    ~MasterStackLayoutEngine() override;

    LayoutKind layoutKind() const override { return LayoutKind::MasterStack; }
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

    qreal masterRatio() const { return m_masterRatio; }
    void setMasterRatio(qreal ratio);

    int masterCount() const { return m_masterCount; }
    void setMasterCount(int count);

    bool supportsPerTileResize() const override { return true; }
    void adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis) override;
    void adjustColumnRatio(qreal delta) override { setMasterRatio(m_masterRatio + delta); }

private:
    int indexOfWindow(Window *window) const;
    // Index of @p window in the master column (0..masters-1) or in the stack
    // column (0..stackCount-1). Returns -1 if the window is not in this engine.
    // The second member is true if the window lives in the master column.
    QPair<int, bool> columnIndexOfWindow(Window *window) const;

    QPointer<RootTile> m_root;
    QList<QPointer<CustomTile>> m_leaves;
    // Per-tile weight inside each column. New leaves default to 1.0. reflow()
    // distributes the column height proportionally to these weights, with a
    // per-leaf floor of m_minWeightShare so a window cannot be collapsed to
    // zero height. The two lists are kept in lock-step with m_leaves, split
    // at the master/stack boundary.
    QList<qreal> m_masterWeights;
    QList<qreal> m_stackWeights;
    qreal m_masterRatio = 0.5;
    int m_masterCount = 1;
    static constexpr qreal m_minWeight = 0.1;
    static constexpr qreal m_maxWeight = 10.0;

    // Source leaf remembered during an interactive drag-move so the window can
    // be swapped with another tiled window or restored to its original place.
    QPointer<CustomTile> m_moveSourceLeaf;
    int m_moveSourceIndex = -1;
};

} // namespace KWin
