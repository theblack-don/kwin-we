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

namespace
{
// Per-tile weight applied to a freshly-added leaf. 1.0 means "takes the same
// share of its column as the other leaves", which is the natural default.
constexpr qreal DefaultWeight = 1.0;

// Minimum share of a column that a single leaf is allowed to take. Prevents
// a user from shrinking a window to zero height by repeated shortcut presses.
// Expressed as a fraction of the column height; 0.05 = 5% of the column.
constexpr qreal MinWeightShare = 0.05;
} // namespace

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

    // The new leaf is appended to the end of the stack column by default. If
    // the master column has spare slots (masterCount > current master count),
    // promote it into master so the master area actually grows.
    const int desiredMasters = qMin(m_masterCount, m_leaves.count() - 1);
    const bool promote = (m_leaves.count() > 1) && (desiredMasters > m_masterWeights.count());
    if (promote) {
        m_masterWeights.append(DefaultWeight);
    } else {
        m_stackWeights.append(DefaultWeight);
    }

    reflow();
}

void MasterStackLayoutEngine::removeWindow(Window *window)
{
    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return;
    }

    // Drop the weight before the leaf so the parallel-list invariant holds.
    const int masterCount = m_masterWeights.count();
    if (idx < masterCount) {
        m_masterWeights.removeAt(idx);
    } else {
        m_stackWeights.removeAt(idx - masterCount);
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
        // A single-window layout has no need for weights, but keep the
        // invariant: one weight in the master list, none in the stack.
        m_masterWeights = {DefaultWeight};
        m_stackWeights.clear();
        Q_EMIT layoutChanged();
        return;
    }

    // Sync the master/stack split with the configured masterCount. The user
    // can change masterCount (e.g. via a future shortcut); we must not lose
    // existing weights when a leaf is promoted from stack to master.
    int desiredMasters = qMin(m_masterCount, count - 1);
    if (m_masterWeights.count() > desiredMasters) {
        // Demote excess masters back to the stack, preserving their weights.
        const int demote = m_masterWeights.count() - desiredMasters;
        for (int i = 0; i < demote; ++i) {
            m_stackWeights.prepend(m_masterWeights.takeLast());
        }
    } else if (m_masterWeights.count() < desiredMasters) {
        // Promote leaves from the stack into the master column.
        const int promote = desiredMasters - m_masterWeights.count();
        for (int i = 0; i < promote && !m_stackWeights.isEmpty(); ++i) {
            m_masterWeights.append(m_stackWeights.takeFirst());
        }
    }

    const int masters = m_masterWeights.count();
    const int stackCount = m_stackWeights.count();
    const qreal masterWidth = m_masterRatio;
    const qreal stackWidth = 1.0 - masterWidth;

    // Compute per-leaf heights by weight, then apply a floor + renormalisation
    // pass so no leaf shrinks below MinWeightShare of its column.
    auto distribute = [](const QList<qreal> &weights, qreal columnH) {
        QList<qreal> heights(weights.size(), 0.0);
        if (weights.isEmpty() || columnH <= 0.0) {
            return heights;
        }
        qreal totalWeight = 0.0;
        for (qreal w : weights) {
            totalWeight += std::max(w, qreal(0.0001));
        }
        for (int i = 0; i < weights.size(); ++i) {
            heights[i] = (std::max(weights[i], qreal(0.0001)) / totalWeight) * columnH;
        }
        // Floor pass: if any leaf is below the share floor, give it the floor
        // and take the deficit from the largest other leaf. Iterate a couple
        // of times so multiple starved leaves are handled.
        const qreal floor = columnH * MinWeightShare;
        for (int pass = 0; pass < 4; ++pass) {
            bool anyStarved = false;
            for (int i = 0; i < heights.size(); ++i) {
                if (heights[i] < floor) {
                    qreal deficit = floor - heights[i];
                    // Find the largest other leaf to take the deficit.
                    int donorIdx = -1;
                    qreal donorH = -1.0;
                    for (int j = 0; j < heights.size(); ++j) {
                        if (j != i && heights[j] > donorH) {
                            donorH = heights[j];
                            donorIdx = j;
                        }
                    }
                    if (donorIdx < 0 || donorH - deficit < floor) {
                        // Cannot satisfy the floor without starving the donor.
                        // Skip this pass; subsequent passes may settle it.
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
        return heights;
    };

    const QList<qreal> masterHeights = distribute(m_masterWeights, 1.0);
    const QList<qreal> stackHeights = distribute(m_stackWeights, 1.0);

    // Master area: left column, vertical distribution by weight.
    qreal masterY = 0.0;
    for (int i = 0; i < masters; ++i) {
        if (CustomTile *leaf = m_leaves[i]) {
            const qreal h = masterHeights.value(i, 1.0 / qMax(masters, 1));
            leaf->setRelativeGeometry(RectF(0.0, masterY, masterWidth, h));
            masterY += h;
        }
    }

    // Stack area: right column, vertical distribution by weight.
    qreal stackY = 0.0;
    for (int i = 0; i < stackCount; ++i) {
        if (CustomTile *leaf = m_leaves[masters + i]) {
            const qreal h = stackHeights.value(i, 1.0 / qMax(stackCount, 1));
            leaf->setRelativeGeometry(RectF(masterWidth, stackY, stackWidth, h));
            stackY += h;
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

void MasterStackLayoutEngine::adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis)
{
    if (axis == Qt::Horizontal) {
        // Horizontal resize on a master/stack layout moves the column divider
        // so the active window's column grows at the expense of the other.
        setMasterRatio(m_masterRatio + weightDelta);
        return;
    }

    // Vertical resize: change the leaf's weight inside its column.
    const QPair<int, bool> location = columnIndexOfWindow(window);
    if (location.first < 0) {
        return;
    }
    QList<qreal> &weights = location.second ? m_masterWeights : m_stackWeights;
    const int idx = location.first;
    weights[idx] = std::clamp(weights[idx] + weightDelta, m_minWeight, m_maxWeight);
    reflow();
}

QPair<int, bool> MasterStackLayoutEngine::columnIndexOfWindow(Window *window) const
{
    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return {-1, false};
    }
    return {idx < m_masterWeights.count() ? idx : idx - m_masterWeights.count(), idx < m_masterWeights.count()};
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
