/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "centertilelayoutengine.h"
#include "customtile.h"
#include "window.h"

#include <QtMath>

#include <algorithm>

namespace KWin
{

namespace
{
// Per-tile weight applied to a freshly-added leaf.
constexpr qreal DefaultWeight = 1.0;

// Minimum share of a column that a single leaf is allowed to take.
constexpr qreal MinWeightShare = 0.05;
} // namespace

CenterTileLayoutEngine::CenterTileLayoutEngine(QObject *parent)
    : LayoutEngine(parent)
{
}

CenterTileLayoutEngine::~CenterTileLayoutEngine() = default;

void CenterTileLayoutEngine::attach(RootTile *root)
{
    m_root = root;
    if (!m_root) {
        return;
    }

    // Take full ownership of the root tile. Remove any pre-existing default
    // layout children that would fight with the engine's geometry calculations.
    const QList<Tile *> existingChildren = m_root->childTiles();
    for (Tile *child : existingChildren) {
        if (CustomTile *custom = qobject_cast<CustomTile *>(child)) {
            m_root->destroyChild(custom);
        }
    }
    m_root->setLayoutDirection(Tile::LayoutDirection::Floating);
    m_root->setRelativeGeometry(RectF(0, 0, 1, 1));
}

void CenterTileLayoutEngine::addWindow(Window *window)
{
    if (!m_root) {
        return;
    }

    // Insert new window as the last item in the layout order. The actual
    // column (left stack / master / right stack) is determined by reflow()
    // based on masterSize and the current window count.
    CustomTile *leaf = m_root->createChildAt(RectF(0, 0, 1, 1), Tile::LayoutDirection::Floating, m_leaves.count());
    m_leaves.append(leaf);
    m_weights.append(DefaultWeight);
    if (!leaf->manage(window)) {
        qWarning() << "CenterTileLayoutEngine: failed to manage window" << window->caption() << "in leaf, desktop mismatch?";
        m_root->destroyChild(leaf);
        m_leaves.removeLast();
        m_weights.removeLast();
        return;
    }

    reflow();
}

void CenterTileLayoutEngine::removeWindow(Window *window)
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

void CenterTileLayoutEngine::pruneEmptyLeaves()
{
    if (!m_root) {
        return;
    }

    bool changed = false;
    // Iterate high-to-low so removing a leaf doesn't shift the indices of
    // leaves we haven't inspected yet.
    for (int i = m_leaves.count() - 1; i >= 0; --i) {
        const auto &leaf = m_leaves[i];
        if (leaf && !leaf->windows().isEmpty()) {
            continue;
        }
        m_weights.removeAt(i);
        if (leaf) {
            m_root->destroyChild(leaf);
        }
        m_leaves.removeAt(i);
        changed = true;
    }

    if (changed) {
        reflow();
    }
}

void CenterTileLayoutEngine::moveWindow(Window *window, int delta)
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
    m_weights.swapItemsAt(idx, newIdx);
    reflow();
}

void CenterTileLayoutEngine::beginMoveWindow(Window *window)
{
    const int idx = indexOfWindow(window);
    if (idx < 0 || idx >= m_leaves.count()) {
        return;
    }
    m_moveSourceLeaf = m_leaves[idx];
    m_moveSourceIndex = idx;
}

bool CenterTileLayoutEngine::endMoveWindow(Window *window, Window *target)
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

void CenterTileLayoutEngine::cancelMoveWindow(Window *window)
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

void CenterTileLayoutEngine::reflow()
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

    // Defensive: keep m_weights in lock-step with m_leaves.
    while (m_weights.size() < count) {
        m_weights.append(DefaultWeight);
    }
    while (m_weights.size() > count) {
        m_weights.removeLast();
    }

    const ColumnSizes sizes = columnSizes(count);

    // Helper: distribute a contiguous run of leaves by their weights over a
    // column's full height, with the same floor/renormalisation strategy
    // used by MasterStackLayoutEngine.
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
        const qreal floor = columnH * MinWeightShare;
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
        return heights;
    };

    // Decide horizontal column widths. When there are no side stacks, the
    // master column takes the full width so windows are still visible. When
    // at least one side stack is populated, masterRatio determines the centre
    // column's width; both side stacks share the remainder equally.
    qreal leftWidth = 0.0;
    qreal masterWidth = 1.0;
    qreal rightWidth = 0.0;
    qreal leftStart = 0.0;
    qreal masterStart = 0.0;
    qreal rightStart = 0.0;

    if (sizes.leftStack > 0 || sizes.rightStack > 0) {
        // CustomTile::setRelativeGeometry bumps each leaf's requested width
        // up to the tile's minimumSize (0.15, see Tile::m_minimumSize). If
        // the requested side-stack width is narrower than that floor and
        // the rightmost column sits at rightStart > (1 - 0.15), the bumped
        // geometry would exceed the screen bounds and be silently rejected
        // by CustomTile — leaving the leaf at its stale full-screen
        // geometry and effectively hiding every other column. Clamp the
        // effective side column width to that floor and shrink the master
        // column to whatever's left so all three columns fit on screen.
        constexpr qreal minColumnWidth = 0.15;
        const qreal requestedMasterWidth = m_masterRatio;
        const qreal requestedSideWidth = (1.0 - requestedMasterWidth) / 2.0;
        const qreal effectiveSideWidth = std::max(requestedSideWidth, minColumnWidth);
        masterWidth = std::max(0.0, 1.0 - 2.0 * effectiveSideWidth);
        leftWidth = effectiveSideWidth;
        rightWidth = effectiveSideWidth;
        leftStart = 0.0;
        masterStart = leftWidth;
        rightStart = leftWidth + masterWidth;
    }

    // Left stack column (if any).
    if (sizes.leftStack > 0) {
        QList<qreal> leftWeights;
        leftWeights.reserve(sizes.leftStack);
        for (int i = 0; i < sizes.leftStack; ++i) {
            leftWeights.append(m_weights[i]);
        }
        const QList<qreal> heights = distribute(leftWeights, 1.0);
        qreal y = 0.0;
        for (int i = 0; i < sizes.leftStack; ++i) {
            if (CustomTile *leaf = m_leaves[i]) {
                leaf->setRelativeGeometry(RectF(leftStart, y, leftWidth, heights[i]));
                y += heights[i];
            }
        }
    }

    // Master column.
    if (sizes.masterColumn > 0) {
        QList<qreal> masterWeights;
        masterWeights.reserve(sizes.masterColumn);
        for (int i = 0; i < sizes.masterColumn; ++i) {
            masterWeights.append(m_weights[sizes.leftStack + i]);
        }
        const QList<qreal> heights = distribute(masterWeights, 1.0);
        qreal y = 0.0;
        for (int i = 0; i < sizes.masterColumn; ++i) {
            if (CustomTile *leaf = m_leaves[sizes.leftStack + i]) {
                leaf->setRelativeGeometry(RectF(masterStart, y, masterWidth, heights[i]));
                y += heights[i];
            }
        }
    }

    // Right stack column (if any).
    if (sizes.rightStack > 0) {
        QList<qreal> rightWeights;
        rightWeights.reserve(sizes.rightStack);
        const int rightStartIdx = sizes.leftStack + sizes.masterColumn;
        for (int i = 0; i < sizes.rightStack; ++i) {
            rightWeights.append(m_weights[rightStartIdx + i]);
        }
        const QList<qreal> heights = distribute(rightWeights, 1.0);
        qreal y = 0.0;
        for (int i = 0; i < sizes.rightStack; ++i) {
            if (CustomTile *leaf = m_leaves[rightStartIdx + i]) {
                leaf->setRelativeGeometry(RectF(rightStart, y, rightWidth, heights[i]));
                y += heights[i];
            }
        }
    }

    Q_EMIT layoutChanged();
}

CenterTileLayoutEngine::ColumnSizes CenterTileLayoutEngine::columnSizes(int windowCount) const
{
    ColumnSizes sizes{0, 0, 0};

    const int effectiveMasters = qMin(m_masterSize, windowCount);
    sizes.masterColumn = effectiveMasters;

    // The remaining windows split equally between the left and right stacks
    // (with the right stack taking the extra one if the count is odd), which
    // matches the truetile CenterTileLayout behaviour:
    //   rstackSize = floor((N - masterSize) / 2)
    //   lstackSize = N - masterSize - rstackSize
    const int remainder = windowCount - effectiveMasters;
    if (remainder > 0) {
        sizes.rightStack = remainder / 2;
        sizes.leftStack = remainder - sizes.rightStack;
    }
    return sizes;
}

int CenterTileLayoutEngine::columnIndexOfWindow(Window *window) const
{
    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return -1;
    }
    const int count = m_leaves.count();
    const ColumnSizes sizes = columnSizes(count);

    // The layout order in m_leaves is: left stack, master column, right stack.
    if (idx < sizes.leftStack) {
        return 0;
    }
    if (idx < sizes.leftStack + sizes.masterColumn) {
        return 1;
    }
    return 2;
}

int CenterTileLayoutEngine::masterIndexOfWindow(Window *window) const
{
    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return -1;
    }
    const ColumnSizes sizes = columnSizes(m_leaves.count());
    const int masterStart = sizes.leftStack;
    const int masterEnd = masterStart + sizes.masterColumn;
    if (idx < masterStart || idx >= masterEnd) {
        return -1;
    }
    return idx - masterStart;
}

void CenterTileLayoutEngine::setMasterRatio(qreal ratio)
{
    ratio = std::clamp(ratio, m_minMasterRatio, m_maxMasterRatio);
    if (qFuzzyCompare(m_masterRatio, ratio)) {
        return;
    }
    m_masterRatio = ratio;
    reflow();
}

void CenterTileLayoutEngine::setMasterSize(int count)
{
    count = std::clamp(count, m_minMasterSize, m_maxMasterSize);
    if (m_masterSize == count) {
        return;
    }
    m_masterSize = count;
    reflow();
}

void CenterTileLayoutEngine::adjustTileSize(Window *window, qreal weightDelta, Qt::Orientation axis)
{
    if (axis == Qt::Horizontal) {
        // Horizontal resize moves the centre column's share of the screen
        // width (and, by symmetry, shrinks/grows both side stacks).
        setMasterRatio(m_masterRatio + weightDelta);
        return;
    }

    // Vertical resize: change the leaf's weight inside its column.
    const int idx = indexOfWindow(window);
    if (idx < 0) {
        return;
    }
    m_weights[idx] = std::clamp(m_weights[idx] + weightDelta, m_minWeight, m_maxWeight);
    reflow();
}

QList<Window *> CenterTileLayoutEngine::windows() const
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

Window *CenterTileLayoutEngine::primaryWindow() const
{
    // The primary window is the topmost master, which is the first leaf
    // after the left stack.
    const int leftCount = columnSizes(m_leaves.count()).leftStack;
    if (leftCount < 0 || leftCount >= m_leaves.count() || !m_leaves[leftCount]) {
        return nullptr;
    }
    const QList<Window *> ws = m_leaves[leftCount]->windows();
    return ws.isEmpty() ? nullptr : ws.first();
}

Window *CenterTileLayoutEngine::windowInDirection(Window *from, FocusDirection direction) const
{
    const QList<Window *> ws = windows();
    if (ws.isEmpty()) {
        return nullptr;
    }

    const int idx = from ? ws.indexOf(from) : -1;
    if (idx < 0) {
        return ws.first();
    }

    const int count = ws.count();
    const ColumnSizes sizes = columnSizes(count);
    const int leftEnd = sizes.leftStack;
    const int masterEnd = leftEnd + sizes.masterColumn;
    const int rightEnd = masterEnd + sizes.rightStack;

    // Helper: validate a candidate index.
    auto neighbour = [&](int candidateIdx) -> Window * {
        if (candidateIdx < 0 || candidateIdx >= count) {
            return nullptr;
        }
        return ws[candidateIdx];
    };

    switch (direction) {
    case FocusDirection::Up: {
        if (idx >= leftEnd && idx < masterEnd) {
            // Master column: step up within it; from master[0] jump to the
            // topmost left-stack leaf (if present).
            const int masterIdx = idx - leftEnd;
            if (masterIdx > 0) {
                return ws[leftEnd + masterIdx - 1];
            }
            if (sizes.leftStack > 0) {
                return ws[sizes.leftStack - 1];
            }
            return nullptr;
        }
        if (idx < leftEnd) {
            // Left stack: step up within it.
            return neighbour(idx - 1);
        }
        // Right stack: step up within it; from top, jump to topmost master.
        if (idx > masterEnd) {
            return neighbour(idx - 1);
        }
        if (idx == masterEnd && sizes.masterColumn > 0) {
            return ws[leftEnd + sizes.masterColumn - 1];
        }
        return nullptr;
    }
    case FocusDirection::Down: {
        if (idx < leftEnd) {
            // Left stack: step down within it; from bottom, jump to top master.
            if (idx < leftEnd - 1) {
                return ws[idx + 1];
            }
            if (sizes.masterColumn > 0) {
                return ws[leftEnd];
            }
            if (sizes.rightStack > 0) {
                return ws[masterEnd];
            }
            return nullptr;
        }
        if (idx >= leftEnd && idx < masterEnd) {
            // Master: step down within it; from bottom, jump to top of right.
            const int masterIdx = idx - leftEnd;
            if (masterIdx < sizes.masterColumn - 1) {
                return ws[leftEnd + masterIdx + 1];
            }
            if (sizes.rightStack > 0) {
                return ws[masterEnd];
            }
            return nullptr;
        }
        // Right stack: step down within it.
        return neighbour(idx + 1);
    }
    case FocusDirection::Left: {
        if (idx >= masterEnd && idx < rightEnd) {
            // Right stack: jump to top of master column.
            if (sizes.masterColumn > 0) {
                return ws[leftEnd];
            }
            if (sizes.leftStack > 0) {
                return ws[sizes.leftStack - 1];
            }
            return nullptr;
        }
        if (idx >= leftEnd && idx < masterEnd) {
            // Master: jump to top of left stack.
            if (sizes.leftStack > 0) {
                return ws[0];
            }
            return nullptr;
        }
        // Already at the leftmost column.
        return nullptr;
    }
    case FocusDirection::Right: {
        if (idx < leftEnd) {
            // Left stack: jump to top of master column.
            if (sizes.masterColumn > 0) {
                return ws[leftEnd];
            }
            if (sizes.rightStack > 0) {
                return ws[masterEnd];
            }
            return nullptr;
        }
        if (idx >= leftEnd && idx < masterEnd) {
            // Master: jump to top of right stack.
            if (sizes.rightStack > 0) {
                return ws[masterEnd];
            }
            return nullptr;
        }
        // Already at the rightmost column.
        return nullptr;
    }
    }

    return nullptr;
}

int CenterTileLayoutEngine::indexOfWindow(Window *window) const
{
    for (int i = 0; i < m_leaves.count(); ++i) {
        if (m_leaves[i] && m_leaves[i]->windows().contains(window)) {
            return i;
        }
    }
    return -1;
}

} // namespace KWin
