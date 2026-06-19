/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tilingcontroller.h"

#include "core/output.h"
#include "core/rect.h"
#include "cursor.h"
#include "tiles/layoutengine.h"
#include "tiles/masterstacklayoutengine.h"
#include "tiles/tilemanager.h"
#include "virtualdesktops.h"
#include "window.h"
#include "workspace.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QtGlobal>

namespace KWin
{

TilingController::TilingController(Workspace *workspace)
    : QObject(workspace)
    , m_workspace(workspace)
    , m_rules(std::make_unique<TilingRules>())
{
    reconfigure();
}

TilingController::~TilingController() = default;

void TilingController::reconfigure()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(KWIN_CONFIG);
    KConfigGroup tilingGroup(config, QStringLiteral("Tiling"));
    KConfigGroup rulesGroup(config, QStringLiteral("TilingRules"));

    m_enabled = tilingGroup.readEntry("Enabled", true);
    m_rules->load(rulesGroup);

    initializeLayouts();
}

void TilingController::initializeLayouts()
{
    if (!m_enabled || !m_workspace) {
        return;
    }

    for (LogicalOutput *output : m_workspace->outputs()) {
        onOutputAdded(output);
    }
}

void TilingController::onOutputAdded(LogicalOutput *output)
{
    if (!m_enabled || !m_workspace || !output) {
        return;
    }

    TileManager *manager = m_workspace->tileManager(output);
    if (!manager) {
        return;
    }
    for (VirtualDesktop *desktop : VirtualDesktopManager::self()->desktops()) {
        setupDefaultLayoutEngine(manager, desktop);
    }
}

void TilingController::setupDefaultLayoutEngine(TileManager *manager, VirtualDesktop *desktop)
{
    if (!manager || !desktop) {
        return;
    }

    // For MVP we always use master-stack. Per-output/desktop layout selection
    // will be added in Phase 2.
    if (manager->layoutEngine(desktop)) {
        return;
    }

    auto engine = std::make_unique<MasterStackLayoutEngine>(manager);
    manager->setLayoutEngine(desktop, std::move(engine));
}

void TilingController::onWindowAdded(Window *window)
{
    if (!m_enabled || !window) {
        return;
    }

    // Don't touch already-managed windows (e.g. on-all-desktops already handled).
    if (window->tilingState().mode != TilingState::Mode::Floating) {
        return;
    }

    TilingState::Mode mode = m_rules->initialMode(window);
    window->tilingState().mode = mode;

    if (mode == TilingState::Mode::Tiled) {
        LogicalOutput *output = window->output() ? window->output() : m_workspace->activeOutput();
        VirtualDesktop *desktop = window->desktops().isEmpty()
            ? VirtualDesktopManager::self()->currentDesktop(output)
            : window->desktops().constFirst();
        addWindowToLayout(window, output, desktop);
    }

    // Watch for mouse-driven moves so we can swap with the window under the
    // cursor on release rather than treating the dragged window as a new one.
    connect(window, &Window::interactiveMoveResizeStarted,
            this, &TilingController::onInteractiveMoveResizeStarted,
            Qt::UniqueConnection);
    connect(window, &Window::interactiveMoveResizeFinished,
            this, &TilingController::onInteractiveMoveResizeFinished,
            Qt::UniqueConnection);
}

void TilingController::onWindowRemoved(Window *window)
{
    if (!window) {
        return;
    }
    removeWindowFromLayouts(window);
}

void TilingController::addWindowToLayout(Window *window, LogicalOutput *output, VirtualDesktop *desktop)
{
    if (!output || !desktop) {
        return;
    }

    TileManager *manager = m_workspace->tileManager(output);
    if (!manager) {
        return;
    }

    setupDefaultLayoutEngine(manager, desktop);

    LayoutEngine *engine = manager->layoutEngine(desktop);
    if (!engine) {
        return;
    }

    engine->addWindow(window);

    // If the window did not end up managed, it will appear floating. Make
    // sure our state reflects reality.
    if (!layoutEngineForWindow(window)) {
        qWarning() << "TilingController: window" << window->caption() << "was not managed by any layout engine";
        window->tilingState().mode = TilingState::Mode::Floating;
    }
}

void TilingController::removeWindowFromLayouts(Window *window)
{
    if (!m_workspace) {
        return;
    }

    for (LogicalOutput *output : m_workspace->outputs()) {
        TileManager *manager = m_workspace->tileManager(output);
        if (!manager) {
            continue;
        }
        for (VirtualDesktop *desktop : VirtualDesktopManager::self()->desktops()) {
            if (LayoutEngine *engine = manager->layoutEngine(desktop)) {
                engine->removeWindow(window);
            }
        }
    }
}

bool TilingController::shouldTile(const Window *window) const
{
    return window && window->tilingState().mode == TilingState::Mode::Tiled;
}

LayoutEngine *TilingController::activeLayoutEngine() const
{
    if (!m_workspace) {
        return nullptr;
    }

    LogicalOutput *output = m_workspace->activeOutput();
    if (!output) {
        return nullptr;
    }

    TileManager *manager = m_workspace->tileManager(output);
    if (!manager) {
        return nullptr;
    }

    return manager->layoutEngine();
}

LayoutEngine *TilingController::layoutEngineForWindow(Window *window, LogicalOutput **output, VirtualDesktop **desktop) const
{
    if (!m_workspace || !window) {
        return nullptr;
    }

    for (LogicalOutput *out : m_workspace->outputs()) {
        TileManager *manager = m_workspace->tileManager(out);
        if (!manager) {
            continue;
        }
        for (VirtualDesktop *desk : VirtualDesktopManager::self()->desktops()) {
            if (LayoutEngine *engine = manager->layoutEngine(desk)) {
                if (engine->windows().contains(window)) {
                    if (output) {
                        *output = out;
                    }
                    if (desktop) {
                        *desktop = desk;
                    }
                    return engine;
                }
            }
        }
    }

    return nullptr;
}

Window *TilingController::activeTiledWindow() const
{
    Window *window = m_workspace ? m_workspace->activeWindow() : nullptr;
    if (window && shouldTile(window)) {
        return window;
    }
    return nullptr;
}

void TilingController::focusLeft()
{
    focusInDirection(LayoutEngine::FocusDirection::Left);
}

void TilingController::focusRight()
{
    focusInDirection(LayoutEngine::FocusDirection::Right);
}

void TilingController::focusUp()
{
    focusInDirection(LayoutEngine::FocusDirection::Up);
}

void TilingController::focusDown()
{
    focusInDirection(LayoutEngine::FocusDirection::Down);
}

void TilingController::focusInDirection(LayoutEngine::FocusDirection direction)
{
    Window *window = activeTiledWindow();
    LayoutEngine *engine = window ? layoutEngineForWindow(window) : activeLayoutEngine();
    if (!engine) {
        return;
    }

    Window *target = engine->windowInDirection(window, direction);
    if (target && m_workspace) {
        m_workspace->activateWindow(target);
    }
}

void TilingController::toggleFloating()
{
    Window *window = activeTiledWindow();
    if (!window) {
        window = m_workspace ? m_workspace->activeWindow() : nullptr;
    }
    if (!window) {
        return;
    }

    TilingState &state = window->tilingState();
    if (state.mode == TilingState::Mode::Tiled) {
        state.mode = TilingState::Mode::Floating;
        state.userToggledFloat = true;
        removeWindowFromLayouts(window);
        // Restore pre-tile geometry if available.
        if (window->geometryRestore().isValid()) {
            window->moveResize(window->geometryRestore());
        }
    } else {
        state.mode = TilingState::Mode::Tiled;
        state.userToggledFloat = false;
        LogicalOutput *output = window->output() ? window->output() : m_workspace->activeOutput();
        VirtualDesktop *desktop = window->desktops().isEmpty()
            ? VirtualDesktopManager::self()->currentDesktop(output)
            : window->desktops().constFirst();
        addWindowToLayout(window, output, desktop);
    }
}

void TilingController::onInteractiveMoveResizeStarted()
{
    Window *window = qobject_cast<Window *>(sender());
    if (!window || !window->isInteractiveMove()) {
        return;
    }
    LayoutEngine *engine = layoutEngineForWindow(window);
    if (!engine) {
        return;
    }

    // Remember where the window came from so we can restore/swap/clean up correctly.
    MoveContext context;
    context.engine = engine;
    context.output = window->output();
    context.originalGeometryRestore = window->geometryRestore();
    m_activeMoves[window] = context;

    // Make the dragged preview smaller (600x800) so it doesn't obscure the
    // whole screen, while still respecting the window's own min/max size.
    constexpr qreal previewWidth = 600.0;
    constexpr qreal previewHeight = 800.0;
    const QSizeF min = window->minSize();
    const QSizeF max = window->maxSize();
    qreal w = std::max(previewWidth, min.width());
    qreal h = std::max(previewHeight, min.height());
    if (max.width() > 0) {
        w = std::min(w, max.width());
    }
    if (max.height() > 0) {
        h = std::min(h, max.height());
    }
    window->setGeometryRestore(RectF(0, 0, w, h));

    engine->beginMoveWindow(window);
}

void TilingController::onInteractiveMoveResizeFinished()
{
    Window *window = qobject_cast<Window *>(sender());
    onWindowMoveFinished(window);
}

void TilingController::onWindowMoveFinished(Window *window)
{
    if (!window || !m_workspace) {
        return;
    }
    // If the user explicitly floated the window, discard any move context.
    if (window->tilingState().mode != TilingState::Mode::Tiled) {
        m_activeMoves.remove(window);
        return;
    }

    auto it = m_activeMoves.find(window);
    if (it != m_activeMoves.end()) {
        MoveContext context = it.value();
        m_activeMoves.erase(it);

        LogicalOutput *currentOutput = window->output() ? window->output() : m_workspace->activeOutput();
        const bool movedToOtherOutput = context.output && currentOutput && context.output != currentOutput;

        if (movedToOtherOutput && context.engine) {
            // The window left its original output. Clean up the empty source
            // tile and add it to the new output's layout engine.
            context.engine->cancelMoveWindow(window);
            VirtualDesktop *desktop = window->desktops().isEmpty()
                ? VirtualDesktopManager::self()->currentDesktop(currentOutput)
                : window->desktops().constFirst();
            addWindowToLayout(window, currentOutput, desktop);
            window->setGeometryRestore(context.originalGeometryRestore);
            return;
        }

        if (context.engine) {
            Window *target = windowUnderCursorInEngine(context.engine);
            if (context.engine->endMoveWindow(window, target)) {
                window->setGeometryRestore(context.originalGeometryRestore);
                return;
            }
        }
        // Engine couldn't handle it (e.g. source tile destroyed); fall through.
    }

    // No move context or engine couldn't handle it: fall back to legacy snap-back.
    if (layoutEngineForWindow(window)) {
        return;
    }
    LogicalOutput *output = window->output() ? window->output() : m_workspace->activeOutput();
    VirtualDesktop *desktop = window->desktops().isEmpty()
        ? VirtualDesktopManager::self()->currentDesktop(output)
        : window->desktops().constFirst();
    addWindowToLayout(window, output, desktop);
}

Window *TilingController::windowUnderCursorInEngine(LayoutEngine *engine) const
{
    if (!m_workspace || !engine) {
        return nullptr;
    }
    const QPointF pos = Cursors::self()->mouse()->pos();
    const QList<Window *> &stacking = m_workspace->stackingOrder();
    for (auto it = stacking.rbegin(); it != stacking.rend(); ++it) {
        Window *window = *it;
        if (window->isDeleted()) {
            continue;
        }
        if (!window->isOnCurrentActivity() || !window->isOnCurrentDesktop()
            || window->isMinimized() || window->isHidden() || window->isHiddenByShowDesktop()) {
            continue;
        }
        if (window->hitTest(pos) && engine->windows().contains(window)) {
            return window;
        }
    }
    return nullptr;
}

void TilingController::promoteToMaster()
{
    Window *window = activeTiledWindow();
    if (!window) {
        return;
    }

    LayoutEngine *engine = layoutEngineForWindow(window);
    if (!engine) {
        return;
    }

    engine->moveWindow(window, -engine->windows().indexOf(window));
}

void TilingController::moveWindowNext()
{
    Window *window = activeTiledWindow();
    if (!window) {
        return;
    }
    LayoutEngine *engine = layoutEngineForWindow(window);
    if (!engine) {
        return;
    }
    engine->moveWindow(window, +1);
}

void TilingController::moveWindowPrevious()
{
    Window *window = activeTiledWindow();
    if (!window) {
        return;
    }
    LayoutEngine *engine = layoutEngineForWindow(window);
    if (!engine) {
        return;
    }
    engine->moveWindow(window, -1);
}

void TilingController::moveWindowToOutput(TilingDirection direction)
{
    Window *window = activeTiledWindow();
    if (!window || !m_workspace) {
        return;
    }

    LogicalOutput *currentOutput = window->output();
    if (!currentOutput) {
        currentOutput = m_workspace->activeOutput();
    }

    Workspace::Direction workspaceDirection = (direction == TilingDirection::West) ? Workspace::DirectionWest : Workspace::DirectionEast;
    LogicalOutput *targetOutput = m_workspace->findOutput(currentOutput, workspaceDirection, true);
    if (!targetOutput || targetOutput == currentOutput) {
        return;
    }

    LogicalOutput *oldOutput = nullptr;
    VirtualDesktop *oldDesktop = nullptr;
    LayoutEngine *oldEngine = layoutEngineForWindow(window, &oldOutput, &oldDesktop);
    if (oldEngine) {
        oldEngine->removeWindow(window);
    }

    // Move window to target output, preserving desktop membership.
    window->sendToOutput(targetOutput);

    VirtualDesktop *desktop = oldDesktop ? oldDesktop : VirtualDesktopManager::self()->currentDesktop(targetOutput);
    addWindowToLayout(window, targetOutput, desktop);
}

} // namespace KWin
