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

namespace
{
constexpr qreal DefaultWeight = 1.0;
constexpr qreal MinWeightShare = 0.05;
constexpr qreal MinWeight = 0.1;
constexpr qreal MaxWeight = 10.0;
} // namespace

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
    m_weights.append(DefaultWeight);
    if (!leaf->manage(window)) {
        qWarning() << "StackedLayoutEngine: failed to manage window" << window->caption() << "in leaf, desktop mismatch?";
        m_root->destroyChild(leaf);
        m_leaves.removeLast();
        m_weights.removeLast();
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

    // Drop the weight before the leaf so the parallel-list invariant holds.
    m_weights.removeAt(idx);
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
        m_weights = {DefaultWeight};
        Q_EMIT layoutChanged();
        return;
    }

    // Defensive: keep m_weights in lock-step with m_leaves. If a reflow is
    // triggered before addWindow has appended a weight, heal the list.
    while (m_weights.size() < count) {
        m_weights.append(DefaultWeight);
    }
    while (m_weights.size() > count) {
        m_weights.removeLast();
    }

    // Distribute the full height by weight. Same floor/renormalisation
    // strategy as MasterStackLayoutEngine.
    QList<qreal> heights(count, 0.0);
    qreal totalWeight = 0.0;
    for (qreal w : m_weights) {
        totalWeight += std::max(w, qreal(0.0001));
    }
    for (int i = 0; i < count; ++i) {
        heights[i] = (std::max(m_weights[i], qreal(0.0001)) / totalWeight) * 1.0;
    }
    const qreal floor = MinWeightShare;
    for (int pass = 0; pass < 4; ++pass) {
        bool anyStarved = false;
        for (int i = 0; i < heights.size(); ++i) {
            if (heights[i] < floor) {
                qreal deficit = floor - heights[i];
                int donorIdx = -1;
                qreal donorH = -1.0;
                for (int j = 0; j < heights.size(); ++j) {
                    if (j != i && heights[j] > donorH) {
                        donorH = heights[j];
                        donorIdx = j;
                    }
                }
                if (donorIdx < 0 || donorH - deficit < floor) {
                    break;
                }
                heights[donorIdx] -= deficit;
                heights[i] = floor;
                anyStarved = true;
            }
        }
        if (!anyStarved) {
            break;
        }
    }

    // Place each leaf from top to bottom. Vertical distribution only;
    // horizontal axis has no meaning for a single-column stack.
    qreal y = 0.0;
    for (int i = 0; i < count; ++i) {
        if (CustomTile *leaf = m_leaves[i]) {
            leaf->setRelativeGeometry(RectF(0.0, y, 1.0, heights[i]));
            y += heights[i];
        }
    }

    Q_EMIT layoutChanged();
}

void StackedLayoutEngine::adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis)
{
    Q_UNUSED(axis); // A single-column stack has no horizontal axis to grow.

    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return;
    }
    m_weights[idx] = std::clamp(m_weights[idx] + weightDelta, MinWeight, MaxWeight);
    reflow();
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
