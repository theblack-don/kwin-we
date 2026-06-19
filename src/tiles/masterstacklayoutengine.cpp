/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "masterstacklayoutengine.h"
#include "customtile.h"
#include "window.h"

#include <QtMath>

#include <algorithm>

namespace KWin
{

MasterStackLayoutEngine::MasterStackLayoutEngine(QObject *parent)
    : LayoutEngine(parent)
{
}

MasterStackLayoutEngine::~MasterStackLayoutEngine() = default;

void MasterStackLayoutEngine::attach(RootTile *root)
{
    m_root = root;
    if (!m_root) {
        return;
    }

    // Take full ownership of the root tile. Any pre-existing default layout
    // children (e.g. the three-column setup created by TileManager) would
    // fight with the engine's geometry calculations, so remove them and make
    // the root a plain floating container that the engine drives directly.
    const QList<Tile *> existingChildren = m_root->childTiles();
    for (Tile *child : existingChildren) {
        if (CustomTile *custom = qobject_cast<CustomTile *>(child)) {
            m_root->destroyChild(custom);
        }
    }
    m_root->setLayoutDirection(Tile::LayoutDirection::Floating);
    m_root->setRelativeGeometry(RectF(0, 0, 1, 1));
}

void MasterStackLayoutEngine::addWindow(Window *window)
{
    if (!m_root) {
        return;
    }

    // Insert new window as the last item in the stack.
    CustomTile *leaf = m_root->createChildAt(RectF(0, 0, 1, 1), Tile::LayoutDirection::Floating, m_leaves.count());
    m_leaves.append(leaf);
    if (!leaf->manage(window)) {
        qWarning() << "MasterStackLayoutEngine: failed to manage window" << window->caption() << "in leaf, desktop mismatch?";
        m_root->destroyChild(leaf);
        m_leaves.removeLast();
        return;
    }

    reflow();
}

void MasterStackLayoutEngine::removeWindow(Window *window)
{
    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return;
    }

    CustomTile *leaf = m_leaves.takeAt(idx);
    leaf->unmanage(window);
    m_root->destroyChild(leaf);

    reflow();
}

void MasterStackLayoutEngine::moveWindow(Window *window, int delta)
{
    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return;
    }

    const int newIdx = qBound(0, idx + delta, m_leaves.count() - 1);
    if (newIdx == idx) {
        return;
    }

    m_leaves.swapItemsAt(idx, newIdx);
    reflow();
}

void MasterStackLayoutEngine::beginMoveWindow(Window *window)
{
    const int idx = indexOfWindow(window);
    if (idx < 0 || idx >= m_leaves.count()) {
        return;
    }
    m_moveSourceLeaf = m_leaves[idx];
    m_moveSourceIndex = idx;
}

bool MasterStackLayoutEngine::endMoveWindow(Window *window, Window *target)
{
    QPointer<CustomTile> sourceLeaf = m_moveSourceLeaf;
    m_moveSourceLeaf.clear();
    m_moveSourceIndex = -1;

    if (!sourceLeaf) {
        return false;
    }

    if (target && target != window) {
        const int targetIdx = indexOfWindow(target);
        if (targetIdx >= 0 && m_leaves[targetIdx]) {
            CustomTile *targetLeaf = m_leaves[targetIdx];
            // Swap the two windows: the dragged window takes the target's leaf,
            // and the target takes the dragged window's original leaf.
            targetLeaf->unmanage(target);
            targetLeaf->manage(window);
            sourceLeaf->manage(target);
            reflow();
            return true;
        }
    }

    // No valid target: restore the window to its original leaf.
    if (!sourceLeaf->windows().contains(window)) {
        sourceLeaf->manage(window);
    }
    reflow();
    return true;
}

void MasterStackLayoutEngine::cancelMoveWindow(Window *window)
{
    QPointer<CustomTile> sourceLeaf = m_moveSourceLeaf;
    m_moveSourceLeaf.clear();
    m_moveSourceIndex = -1;

    if (!sourceLeaf || !m_leaves.contains(sourceLeaf)) {
        return;
    }

    // The window moved to another output. Remove the now-empty source leaf
    // so the original layout reflows and doesn't leave a gap behind.
    if (sourceLeaf->windows().isEmpty() || sourceLeaf->windows().contains(window)) {
        if (sourceLeaf->windows().contains(window)) {
            sourceLeaf->unmanage(window);
        }
        m_leaves.removeOne(sourceLeaf);
        m_root->destroyChild(sourceLeaf);
        reflow();
    }
}

void MasterStackLayoutEngine::reflow()
{
    if (!m_root || m_leaves.isEmpty()) {
        return;
    }

    const int count = m_leaves.count();

    if (count == 1) {
        if (CustomTile *leaf = m_leaves.first()) {
            leaf->setRelativeGeometry(RectF(0, 0, 1, 1));
        }
        Q_EMIT layoutChanged();
        return;
    }

    // Clamp master count to at most count - 1 so there is always a stack.
    const int masters = qMin(m_masterCount, count - 1);
    const qreal masterWidth = m_masterRatio;
    const qreal stackWidth = 1.0 - masterWidth;
    const int stackCount = count - masters;
    const qreal stackTileHeight = 1.0 / stackCount;

    // Master area: left column, vertically split among master windows.
    const qreal masterTileHeight = 1.0 / masters;
    for (int i = 0; i < masters; ++i) {
        if (CustomTile *leaf = m_leaves[i]) {
            RectF geom(0.0, i * masterTileHeight, masterWidth, masterTileHeight);
            leaf->setRelativeGeometry(geom);
        }
    }

    // Stack area: right column, vertical split.
    for (int i = 0; i < stackCount; ++i) {
        if (CustomTile *leaf = m_leaves[masters + i]) {
            RectF geom(masterWidth, i * stackTileHeight, stackWidth, stackTileHeight);
            leaf->setRelativeGeometry(geom);
        }
    }

    Q_EMIT layoutChanged();
}

void MasterStackLayoutEngine::setMasterRatio(qreal ratio)
{
    ratio = std::clamp(ratio, 0.1, 0.9);
    if (qFuzzyCompare(m_masterRatio, ratio)) {
        return;
    }
    m_masterRatio = ratio;
    reflow();
}

void MasterStackLayoutEngine::setMasterCount(int count)
{
    count = std::max(count, 1);
    if (m_masterCount == count) {
        return;
    }
    m_masterCount = count;
    reflow();
}

QList<Window *> MasterStackLayoutEngine::windows() const
{
    QList<Window *> result;
    result.reserve(m_leaves.count());
    for (const auto &leaf : m_leaves) {
        if (leaf && !leaf->windows().isEmpty()) {
            result.append(leaf->windows().first());
        }
    }
    return result;
}

Window *MasterStackLayoutEngine::primaryWindow() const
{
    if (m_leaves.isEmpty() || !m_leaves.first()) {
        return nullptr;
    }
    const QList<Window *> ws = m_leaves.first()->windows();
    return ws.isEmpty() ? nullptr : ws.first();
}

Window *MasterStackLayoutEngine::windowInDirection(Window *from, FocusDirection direction) const
{
    const QList<Window *> ws = windows();
    if (ws.isEmpty()) {
        return nullptr;
    }

    const int idx = from ? ws.indexOf(from) : -1;
    if (idx < 0) {
        return ws.first();
    }

    switch (direction) {
    case FocusDirection::Left:
        // From any stack window, focus the master window.
        if (idx == 0) {
            return nullptr;
        }
        return ws[0];
    case FocusDirection::Right:
        // From the master, focus the top stack window.
        if (idx == 0) {
            return ws.count() > 1 ? ws[1] : nullptr;
        }
        return nullptr;
    case FocusDirection::Up:
        if (idx > 0) {
            return ws[idx - 1];
        }
        // From master, wrap to the top of the stack.
        return ws.count() > 1 ? ws[1] : nullptr;
    case FocusDirection::Down:
        if (idx < ws.count() - 1) {
            return ws[idx + 1];
        }
        // From the bottom of the stack, wrap back to master.
        return ws[0];
    }

    return nullptr;
}

int MasterStackLayoutEngine::indexOfWindow(Window *window) const
{
    for (int i = 0; i < m_leaves.count(); ++i) {
        if (m_leaves[i] && m_leaves[i]->windows().contains(window)) {
            return i;
        }
    }
    return -1;
}

} // namespace KWin
