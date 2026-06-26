/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/rect.h"
#include "kwin_export.h"

#include <QList>
#include <QObject>

namespace KWin
{

class RootTile;
class Window;

/**
 * Abstract interface for tiling layout algorithms.
 *
 * A LayoutEngine is attached to a RootTile. It owns the creation and destruction
 * of the leaf tiles under that root, and it decides their geometries. It does not
 * touch Window objects directly; TilingController adds/removes windows to/from the
 * engine, and the engine routes the windows into Tile leaves.
 */
class KWIN_EXPORT LayoutEngine : public QObject
{
    Q_OBJECT

public:
    /**
     * Identifies a layout engine implementation. The integer value is what is
     * stored in kwinrc; append new kinds to the end and never reorder.
     */
    enum class LayoutKind {
        MasterStack = 0,
        Stacked = 1,
        CenterTile = 2,
    };

    static QString layoutKindToString(LayoutKind kind);
    static LayoutKind layoutKindFromString(const QString &name, LayoutKind fallback = LayoutKind::MasterStack);

    explicit LayoutEngine(QObject *parent = nullptr);
    ~LayoutEngine() override;

    /**
     * Returns the kind of layout implemented by this engine.
     */
    virtual LayoutKind layoutKind() const = 0;

    /**
     * Called once when the engine is assigned to a RootTile.
     */
    virtual void attach(RootTile *root) = 0;

    /**
     * Window lifecycle hooks.
     */
    virtual void addWindow(Window *window) = 0;
    virtual void removeWindow(Window *window) = 0;

    /**
     * Move a window by a delta in the layout order.
     * Positive delta moves forward in the order.
     */
    virtual void moveWindow(Window *window, int delta) = 0;

    /**
     * Called when an interactive move of a tiled window starts.
     * The engine should remember the window's source position so it can be
     * restored or swapped on release.
     */
    virtual void beginMoveWindow(Window *window) { Q_UNUSED(window) }

    /**
     * Called when an interactive move of a tiled window ends.
     * If @p target is non-null, the dragged @p window should take the target's
     * place and the target should take the window's original place.
     * If @p target is null, the window should be restored to its original place.
     * Returns true if the engine handled the window.
     */
    virtual bool endMoveWindow(Window *window, Window *target) { Q_UNUSED(window) Q_UNUSED(target) return false; }

    /**
     * Called when a dragged tiled window was moved to a different output and
     * should be removed from this engine's layout. The engine should clean up
     * the empty source tile and reflow the remaining windows.
     */
    virtual void cancelMoveWindow(Window *window) { Q_UNUSED(window) }

    /**
     * Remove any leaves whose window list is empty (e.g. a window that was
     * detached by an outside path such as Window::exitQuickTileMode(), which
     * maximized windows go through). The orphaned empty leaf is destroyed and
     * the remaining leaves reflow to fill the gap. No-op when there are no
     * empty leaves.
     */
    virtual void pruneEmptyLeaves() { }

    /**
     * Recompute all tile geometries. Called after config changes, output resize,
     * or when the engine's internal order changes.
     */
    virtual void reflow() = 0;

    /**
     * Returns all tiled windows managed by this engine in layout order.
     */
    virtual QList<Window *> windows() const = 0;

    /**
     * Returns the first window in the layout (e.g. the master window).
     */
    virtual Window *primaryWindow() const = 0;

    /**
     * Scrolling-layout support. Bounded layouts return false and ignore viewport.
     */
    virtual bool supportsViewport() const { return false; }
    virtual void setViewportOffset(qreal offset) { Q_UNUSED(offset) }
    virtual qreal viewportOffset() const { return 0.0; }

    /**
     * Scrolling-specific column actions. No-op for non-scrolling layouts.
     */
    virtual void focusColumnLeft() {}
    virtual void focusColumnRight() {}
    virtual void moveColumnLeft() {}
    virtual void moveColumnRight() {}
    virtual void switchPresetColumnWidth() {}

    /**
     * Per-tile resize support. Bounded layouts return false and ignore
     * adjustTileSize / adjustColumnRatio.
     *
     * @p weightDelta is in weight units (additive). The engine clamps the new
     * weight to a sane range internally and reflows the affected column.
     * @p axis is the direction of the resize. For a master/stack layout, the
     * horizontal axis moves the column divider; the vertical axis moves the
     * active leaf's share within its column. For a single-column stacked
     * layout, only the vertical axis is meaningful.
     */
    virtual bool supportsPerTileResize() const { return false; }
    virtual void adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis)
    {
        Q_UNUSED(window)
        Q_UNUSED(weightDelta)
        Q_UNUSED(axis)
    }
    virtual void adjustColumnRatio(qreal delta) { Q_UNUSED(delta) }

    /**
     * Directional focus support. Returns the window in the requested direction
     * relative to @p from, or nullptr if there is no window in that direction.
     * If @p from is nullptr, returns the primary/first window.
     */
    enum class FocusDirection {
        Left,
        Right,
        Up,
        Down,
    };
    virtual Window *windowInDirection(Window *from, FocusDirection direction) const { Q_UNUSED(from) Q_UNUSED(direction) return nullptr; }

Q_SIGNALS:
    /**
     * Emitted when the layout geometry changes and the scene may need update.
     */
    void layoutChanged();
};

} // namespace KWin
