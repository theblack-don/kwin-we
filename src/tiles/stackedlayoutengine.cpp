/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stackedlayoutengine.h"
#include "customtile.h"
#include "window.h"

#include <algorithm>

namespace KWin
{

StackedLayoutEngine::StackedLayoutEngine(QObject *parent)
    : LayoutEngine(parent)
{
}

StackedLayoutEngine::~StackedLayoutEngine() = default;

void StackedLayoutEngine::attach(RootTile *root)
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

void StackedLayoutEngine::addWindow(Window *window)
{
    if (!m_root) {
        return;
    }

    // Insert new window as the last item in the stack.
    CustomTile *leaf = m_root->createChildAt(RectF(0, 0, 1, 1), Tile::LayoutDirection::Floating, m_leaves.count());
    m_leaves.append(leaf);
    if (!leaf->manage(window)) {
        qWarning() << "StackedLayoutEngine: failed to manage window" << window->caption() << "in leaf, desktop mismatch?";
        m_root->destroyChild(leaf);
        m_leaves.removeLast();
        return;
    }

    reflow();
}

void StackedLayoutEngine::removeWindow(Window *window)
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

void StackedLayoutEngine::moveWindow(Window *window, int delta)
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

void StackedLayoutEngine::beginMoveWindow(Window *window)
{
    const int idx = indexOfWindow(window);
    if (idx < 0 || idx >= m_leaves.count()) {
        return;
    }
    m_moveSourceLeaf = m_leaves[idx];
    m_moveSourceIndex = idx;
}

bool StackedLayoutEngine::endMoveWindow(Window *window, Window *target)
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

void StackedLayoutEngine::cancelMoveWindow(Window *window)
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

void StackedLayoutEngine::reflow()
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

    // Stack fills the full width; height is split evenly among the leaves.
    const qreal stripHeight = 1.0 / count;
    for (int i = 0; i < count; ++i) {
        if (CustomTile *leaf = m_leaves[i]) {
            RectF geom(0.0, i * stripHeight, 1.0, stripHeight);
            leaf->setRelativeGeometry(geom);
        }
    }

    Q_EMIT layoutChanged();
}

QList<Window *> StackedLayoutEngine::windows() const
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

Window *StackedLayoutEngine::primaryWindow() const
{
    if (m_leaves.isEmpty() || !m_leaves.first()) {
        return nullptr;
    }
    const QList<Window *> ws = m_leaves.first()->windows();
    return ws.isEmpty() ? nullptr : ws.first();
}

Window *StackedLayoutEngine::windowInDirection(Window *from, FocusDirection direction) const
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
    case FocusDirection::Up:
        if (idx > 0) {
            return ws[idx - 1];
        }
        return nullptr;
    case FocusDirection::Down:
        if (idx < ws.count() - 1) {
            return ws[idx + 1];
        }
        return nullptr;
    case FocusDirection::Left:
    case FocusDirection::Right:
        // No horizontal neighbours in a pure single-column stack.
        return nullptr;
    }

    return nullptr;
}

int StackedLayoutEngine::indexOfWindow(Window *window) const
{
    for (int i = 0; i < m_leaves.count(); ++i) {
        if (m_leaves[i] && m_leaves[i]->windows().contains(window)) {
            return i;
        }
    }
    return -1;
}

} // namespace KWin
