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

private:
    int indexOfWindow(Window *window) const;

    QPointer<RootTile> m_root;
    QList<QPointer<CustomTile>> m_leaves;
    qreal m_masterRatio = 0.5;
    int m_masterCount = 1;

    // Source leaf remembered during an interactive drag-move so the window can
    // be swapped with another tiled window or restored to its original place.
    QPointer<CustomTile> m_moveSourceLeaf;
    int m_moveSourceIndex = -1;
};

} // namespace KWin
