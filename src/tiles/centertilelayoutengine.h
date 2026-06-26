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
 * Center tile layout.
 *
 * A three-column tiling layout where the master (focused) windows occupy a
 * centred column and any extra windows form two stacks, one on the left and
 * one on the right. The two side stacks share the remaining screen width
 * equally.
 *
 * The number of master windows in the centre column is controlled by
 * setMasterSize() (default 1). The relative width of the centre column
 * versus the side stacks is controlled by setMasterRatio() (default 0.6).
 *
 * Layout cases by number of tiled windows N:
 *   N <= masterSize
 *       All windows stacked vertically in the centre column. The two side
 *       stacks are empty (zero width).
 *   N == masterSize + 1
 *       The first masterSize windows fill the centre column (left side of
 *       the screen at masterRatio width). The remaining window fills the
 *       right side, occupying the rest of the screen.
 *   N > masterSize + 1
 *       Three columns: left stack, centre master column, right stack. The
 *       side stacks share the remaining width equally.
 *
 * Inspired by the CenterTileLayout class in the truetile kwin script
 * (https://github.com/truegil/truetile).
 */
class KWIN_EXPORT CenterTileLayoutEngine : public LayoutEngine
{
    Q_OBJECT

public:
    explicit CenterTileLayoutEngine(QObject *parent = nullptr);
    ~CenterTileLayoutEngine() override;

    LayoutKind layoutKind() const override { return LayoutKind::CenterTile; }
    void attach(RootTile *root) override;
    void addWindow(Window *window) override;
    void removeWindow(Window *window) override;
    void moveWindow(Window *window, int delta) override;
    void beginMoveWindow(Window *window) override;
    bool endMoveWindow(Window *window, Window *target) override;
    void cancelMoveWindow(Window *window) override;
    void reflow() override;
    void pruneEmptyLeaves() override;

    QList<Window *> windows() const override;
    Window *primaryWindow() const override;
    Window *windowInDirection(Window *from, FocusDirection direction) const override;

    qreal masterRatio() const { return m_masterRatio; }
    void setMasterRatio(qreal ratio);

    int masterSize() const { return m_masterSize; }
    void setMasterSize(int count);

    bool supportsPerTileResize() const override { return true; }
    void adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis) override;
    void adjustColumnRatio(qreal delta) override { setMasterRatio(m_masterRatio + delta); }

private:
    int indexOfWindow(Window *window) const;

    /**
     * Split @p windowCount tiles into left-stack, master-column, right-stack
     * groups according to masterSize. Returns the size of each group.
     */
    struct ColumnSizes {
        int leftStack;
        int masterColumn;
        int rightStack;
    };
    ColumnSizes columnSizes(int windowCount) const;

    /**
     * Index of @p window inside the master column, or -1 if it lives in a
     * side stack. Used for vertical resize within the master column.
     */
    int masterIndexOfWindow(Window *window) const;
    /**
     * Returns 0 for left stack, 1 for master column, 2 for right stack.
     * Returns -1 if the window is not in this engine.
     */
    int columnIndexOfWindow(Window *window) const;

    QPointer<RootTile> m_root;
    QList<QPointer<CustomTile>> m_leaves;
    // Per-tile weight parallel to m_leaves. Each new leaf starts at 1.0;
    // reflow() distributes each column's height proportionally to its
    // members' weights. See the distribute() helper in reflow() for the
    // floor/renormalisation details.
    QList<qreal> m_weights;

    // Default master-width fraction of the output (0.6 in truetile; bumped to
    // 0.75 here so the centre column is wider out of the box). The KCM
    // exposes this as a percentage (CenterTileMasterWidth) and clamps it
    // to [20, 95].
    qreal m_masterRatio = 0.75;
    int m_masterSize = 1;
    // Bounds for setMasterRatio(). The lower bound matches truetile
    // (0.2); the upper bound is bumped from truetile's 0.75 to 0.95 so the
    // user can make the centre column as wide as Image 2 in the design
    // screenshots.
    static constexpr qreal m_minMasterRatio = 0.2;
    static constexpr qreal m_maxMasterRatio = 0.95;
    static constexpr int m_minMasterSize = 1;
    static constexpr int m_maxMasterSize = 10;
    static constexpr qreal m_minWeight = 0.1;
    static constexpr qreal m_maxWeight = 10.0;

    // Source leaf remembered during an interactive drag-move so the window
    // can be swapped with another tiled window or restored to its original
    // place.
    QPointer<CustomTile> m_moveSourceLeaf;
    int m_moveSourceIndex = -1;
};

} // namespace KWin
