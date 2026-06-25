/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "core/output.h"
#include "input.h"
#include "pointer_input.h"
#include "tiles/layoutengine.h"
#include "tiles/masterstacklayoutengine.h"
#include "tiles/tilemanager.h"
#include "tiling/tilingcontroller.h"
#include "tiling/tilingstate.h"
#include "virtualdesktops.h"
#include "wayland_server.h"
#include "window.h"
#include "workspace.h"

#include <QSignalSpy>

namespace KWin
{

/**
 * Integration tests for the fork's TilingController.
 *
 * Specifically, these tests pin down the behavior of the
 * "Window One Desktop Down/Up/Left/Right" and "Window One Screen
 * Left/Right" shortcuts. The bug being fixed: when a tiled window is
 * sent to a different desktop (or screen) via the keyboard shortcut,
 * the window ends up floating on the destination instead of being
 * re-tiled, and the source desktop is left with a hole.
 */
class TilingControllerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSingleWindowMoveBetweenDesktops();
    void testMultiWindowSourceReflow();
    void testCrossOutputMove();
    void testFirstTimeEngineCreationOnDestination();
    void testNoOpWhenDesktopUnchanged();
    void testNoIntermediateMigrationDuringInteractiveMove();
    void testMinimizeRemovesFromTiling();
    void testUnminimizeRejoinsTiling();
    void testMaximizeReleasesFromTiling();
    void testUnmaximizeRejoinsTiling();

private:
    std::unique_ptr<KWayland::Client::Surface> m_surface;
    std::unique_ptr<KWayland::Client::Surface> m_surface2;
    // We must keep the toplevel objects alive for the lifetime of the
    // windows they produced, otherwise the windows get torn down.
    std::unique_ptr<Test::XdgToplevel> m_shellSurface;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface2;
    Window *m_window = nullptr;
    Window *m_window2 = nullptr;

    LogicalOutput *m_output = nullptr;
    TileManager *m_tileManager = nullptr;
    VirtualDesktop *m_desktop1 = nullptr;
    VirtualDesktop *m_desktop2 = nullptr;
};

void TilingControllerTest::initTestCase()
{
    qRegisterMetaType<KWin::Window *>();
    QVERIFY(waylandServer()->init(qAppName()));

    kwinApp()->start();
    Test::setOutputConfig({
        Rect(0, 0, 1280, 1024),
        Rect(1280, 0, 1280, 1024),
    });
    setenv("QT_QPA_PLATFORM", "wayland", true);
}

void TilingControllerTest::init()
{
    QVERIFY(Test::setupWaylandConnection());

    workspace()->setActiveOutput(QPoint(640, 512));
    input()->pointer()->warp(QPoint(640, 512));

    m_output = workspace()->activeOutput();
    m_tileManager = workspace()->tileManager(m_output);
    QVERIFY(m_tileManager);

    // Ensure we have at least two virtual desktops so we can move between
    // them. TilingController layout engines are created per (output, desktop)
    // pair, so this exercises the cross-desktop path.
    VirtualDesktopManager::self()->setCount(3);
    const auto desktops = VirtualDesktopManager::self()->desktops();
    QCOMPARE(desktops.size(), 3);
    m_desktop1 = desktops.at(0);
    m_desktop2 = desktops.at(1);
    VirtualDesktopManager::self()->setCurrent(m_desktop1);
}

void TilingControllerTest::cleanup()
{
    m_window = nullptr;
    m_window2 = nullptr;
    m_shellSurface.reset();
    m_shellSurface2.reset();
    m_surface.reset();
    m_surface2.reset();
    Test::destroyWaylandConnection();
}

void TilingControllerTest::testSingleWindowMoveBetweenDesktops()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    m_surface = Test::createSurface();
    QVERIFY(m_surface);
    m_shellSurface = Test::createXdgToplevelSurface(m_surface.get());
    m_window = Test::renderAndWaitForShown(m_surface.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window);

    // Default rules: a normal xdg-toplevel window should be tiled.
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    // The window should be in the engine for the current desktop on the
    // active output.
    LayoutEngine *engineBefore = m_tileManager->layoutEngine(m_desktop1);
    QVERIFY(engineBefore);
    QVERIFY(engineBefore->windows().contains(m_window));

    // Now move the window to desktop 2 (the same way the "Window One Desktop
    // Down" shortcut does, via setDesktops). The desktopsChanged signal
    // triggers the migration in TilingController::onWindowDesktopsChanged.
    workspace()->sendWindowToDesktops(m_window, {m_desktop2}, true);
    QCOMPARE(m_window->desktops(), QList<VirtualDesktop *>{m_desktop2});

    // The window must STILL be Tiled (the regression we are fixing).
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    // And it must now live in the engine for desktop 2.
    LayoutEngine *engineAfter = m_tileManager->layoutEngine(m_desktop2);
    QVERIFY(engineAfter);
    QVERIFY(engineAfter->windows().contains(m_window));
    QVERIFY(!engineBefore->windows().contains(m_window));
}

void TilingControllerTest::testMultiWindowSourceReflow()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // Two tiled windows on desktop 1.
    m_surface = Test::createSurface();
    m_shellSurface = Test::createXdgToplevelSurface(m_surface.get());
    m_window = Test::renderAndWaitForShown(m_surface.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    m_surface2 = Test::createSurface();
    m_shellSurface2 = Test::createXdgToplevelSurface(m_surface2.get());
    m_window2 = Test::renderAndWaitForShown(m_surface2.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window2);
    QCOMPARE(m_window2->tilingState().mode, TilingState::Mode::Tiled);
    QVERIFY(m_window != m_window2);

    LayoutEngine *engineD1 = m_tileManager->layoutEngine(m_desktop1);
    QVERIFY(engineD1);

    // Both windows live on desktop 1, in the same engine.
    QVERIFY2(engineD1->windows().contains(m_window),
             qPrintable(QStringLiteral("m_window not in engineD1. Engine has %1 windows")
                        .arg(engineD1->windows().size())));
    QVERIFY2(engineD1->windows().contains(m_window2),
             qPrintable(QStringLiteral("m_window2 not in engineD1. Engine has %1 windows")
                        .arg(engineD1->windows().size())));
    QCOMPARE(engineD1->windows().size(), 2);

    // Move m_window to desktop 2.
    workspace()->sendWindowToDesktops(m_window, {m_desktop2}, true);
    QCOMPARE(m_window->desktops(), QList<VirtualDesktop *>{m_desktop2});

    // m_window must still be Tiled and now on desktop 2's engine.
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);
    LayoutEngine *engineD2 = m_tileManager->layoutEngine(m_desktop2);
    QVERIFY(engineD2);
    QVERIFY2(engineD2->windows().contains(m_window),
             qPrintable(QStringLiteral("m_window not in engineD2 after move. engineD2 has %1 windows")
                        .arg(engineD2->windows().size())));
    QVERIFY(!engineD1->windows().contains(m_window));

    // m_window2 remains on desktop 1 in the reflowed engine.
    QVERIFY(engineD1->windows().contains(m_window2));
    QCOMPARE(engineD1->windows().size(), 1);
}

void TilingControllerTest::testCrossOutputMove()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    const auto outputs = workspace()->outputs();
    QCOMPARE(outputs.size(), 2);

    LogicalOutput *sourceOutput = outputs.at(0);
    LogicalOutput *targetOutput = outputs.at(1);
    TileManager *sourceManager = workspace()->tileManager(sourceOutput);
    TileManager *targetManager = workspace()->tileManager(targetOutput);
    QVERIFY(sourceManager);
    QVERIFY(targetManager);

    m_surface = Test::createSurface();
    QVERIFY(m_surface);
    m_shellSurface = Test::createXdgToplevelSurface(m_surface.get());
    m_window = Test::renderAndWaitForShown(m_surface.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    // Move to the other output via the public controller API.
    controller->moveWindowToOutput(TilingController::TilingDirection::East);

    // Window must remain Tiled and now live in an engine on the target output.
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);
    LayoutEngine *targetEngine = targetManager->layoutEngine(m_desktop1);
    QVERIFY(targetEngine);
    QVERIFY(targetEngine->windows().contains(m_window));

    LayoutEngine *sourceEngine = sourceManager->layoutEngine(m_desktop1);
    QVERIFY(sourceEngine);
    QVERIFY(!sourceEngine->windows().contains(m_window));
}

void TilingControllerTest::testFirstTimeEngineCreationOnDestination()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // We want a destination desktop whose layout engine doesn't exist yet.
    // Set the count to exactly 1, place the window there, then bump the
    // count to 2 to create a fresh desktop with no engine.
    VirtualDesktopManager::self()->setCount(1);
    m_desktop1 = VirtualDesktopManager::self()->desktops().at(0);
    VirtualDesktopManager::self()->setCurrent(m_desktop1);

    m_surface = Test::createSurface();
    QVERIFY(m_surface);
    m_shellSurface = Test::createXdgToplevelSurface(m_surface.get());
    m_window = Test::renderAndWaitForShown(m_surface.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    // Now create a second desktop. No engine exists for it yet.
    VirtualDesktopManager::self()->setCount(2);
    m_desktop2 = VirtualDesktopManager::self()->desktops().at(1);

    TileManager *manager = workspace()->tileManager(m_output);
    QVERIFY(!manager->layoutEngine(m_desktop2));

    // Move the window to the new desktop. The migration helper must create
    // the engine on demand via addWindowToLayout -> setupLayoutEngine.
    workspace()->sendWindowToDesktops(m_window, {m_desktop2}, true);
    QCOMPARE(m_window->desktops(), QList<VirtualDesktop *>{m_desktop2});

    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    LayoutEngine *newEngine = manager->layoutEngine(m_desktop2);
    QVERIFY(newEngine);
    QVERIFY(newEngine->windows().contains(m_window));
}

void TilingControllerTest::testNoOpWhenDesktopUnchanged()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    m_surface = Test::createSurface();
    QVERIFY(m_surface);
    m_shellSurface = Test::createXdgToplevelSurface(m_surface.get());
    m_window = Test::renderAndWaitForShown(m_surface.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    LayoutEngine *engineBefore = m_tileManager->layoutEngine(m_desktop1);
    QVERIFY(engineBefore);
    QVERIFY(engineBefore->windows().contains(m_window));
    const int leavesBefore = engineBefore->windows().size();

    // Setting the same desktop twice is a no-op. The migration helper
    // detects this and returns early.
    workspace()->sendWindowToDesktops(m_window, {m_desktop1}, true);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);
    QVERIFY(engineBefore->windows().contains(m_window));
    QCOMPARE(engineBefore->windows().size(), leavesBefore);
}

void TilingControllerTest::testNoIntermediateMigrationDuringInteractiveMove()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // Three horizontal outputs with per-output virtual desktops, matching the
    // user report of dragging across three monitors.
    Test::setOutputConfig({
        Rect(0, 0, 1280, 1024),
        Rect(1280, 0, 1280, 1024),
        Rect(2560, 0, 1280, 1024),
    });
    VirtualDesktopManager::self()->setPerOutputVirtualDesktops(true);
    const auto outputs = workspace()->outputs();
    QCOMPARE(outputs.size(), 3);
    const auto desktops = VirtualDesktopManager::self()->desktops();
    VirtualDesktopManager::self()->setCurrent(desktops.at(0), outputs.at(0));
    VirtualDesktopManager::self()->setCurrent(desktops.at(1), outputs.at(1));
    VirtualDesktopManager::self()->setCurrent(desktops.at(2), outputs.at(2));

    LogicalOutput *output1 = outputs.at(0);
    LogicalOutput *output2 = outputs.at(1);
    LogicalOutput *output3 = outputs.at(2);
    TileManager *manager1 = workspace()->tileManager(output1);
    TileManager *manager2 = workspace()->tileManager(output2);
    TileManager *manager3 = workspace()->tileManager(output3);
    QVERIFY(manager1);
    QVERIFY(manager2);
    QVERIFY(manager3);

    // Create the dragged window on output 1.
    input()->pointer()->warp(output1->geometry().center());
    m_surface = Test::createSurface();
    QVERIFY(m_surface);
    m_shellSurface = Test::createXdgToplevelSurface(m_surface.get());
    m_window = Test::renderAndWaitForShown(m_surface.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);
    LayoutEngine *engine1 = manager1->layoutEngine(desktops.at(0));
    QVERIFY(engine1);
    QVERIFY(engine1->windows().contains(m_window));

    // Start interactive move.
    QSignalSpy interactiveMoveResizeStartedSpy(m_window, &Window::interactiveMoveResizeStarted);
    workspace()->activateWindow(m_window);
    workspace()->slotWindowMove();
    QCOMPARE(interactiveMoveResizeStartedSpy.count(), 1);
    QCOMPARE(m_window->isInteractiveMove(), true);

    // First step lands on output 2. For a tiled window this un-tiles the
    // window and restores it to geometryRestore (the small drag preview).
    const QPointF pos2(output2->geometry().center());
    input()->pointer()->warp(pos2.toPoint());
    m_window->updateInteractiveMoveResize(pos2, Qt::KeyboardModifiers());
    QCOMPARE(m_window->quickTileMode(), QuickTileMode(QuickTileFlag::None));

    // Second step is also on output 2; this is where moveResizeOutput would
    // become output 2 and emit outputChanged, which (with per-output desktops)
    // changes the desktop and would trigger the migration we are guarding.
    const QPointF pos2b(1700, 512);
    input()->pointer()->warp(pos2b.toPoint());
    m_window->updateInteractiveMoveResize(pos2b, Qt::KeyboardModifiers());
    QCOMPARE(m_window->output(), output2);
    QCOMPARE(m_window->desktops(), QList<VirtualDesktop *>{desktops.at(1)});

    // The regression: the dragged window must NOT be added to output 2's layout.
    // If the bug were present, onWindowDesktopsChanged would have migrated the
    // window here while it was still being dragged.
    if (LayoutEngine *engine2 = manager2->layoutEngine(m_window->desktops().constFirst())) {
        QVERIFY2(!engine2->windows().contains(m_window),
                 qPrintable(QStringLiteral("Dragged window leaked into intermediate output 2's layout")));
    }

    // Move onto output 3.
    const QPointF pos3(output3->geometry().center());
    input()->pointer()->warp(pos3.toPoint());
    m_window->updateInteractiveMoveResize(pos3, Qt::KeyboardModifiers());
    QCOMPARE(m_window->output(), output3);
    QCOMPARE(m_window->desktops(), QList<VirtualDesktop *>{desktops.at(2)});
    if (LayoutEngine *engine2 = manager2->layoutEngine(desktops.at(1))) {
        QVERIFY(!engine2->windows().contains(m_window));
    }

    // Finish the move on output 3.
    QSignalSpy interactiveMoveResizeFinishedSpy(m_window, &Window::interactiveMoveResizeFinished);
    m_window->keyPressEvent(Qt::Key_Enter);
    QCOMPARE(interactiveMoveResizeFinishedSpy.count(), 1);
    QCOMPARE(m_window->isInteractiveMove(), false);

    // The window should end up in output 3's layout.
    LayoutEngine *engine3 = manager3->layoutEngine(desktops.at(2));
    QVERIFY(engine3);
    QVERIFY2(engine3->windows().contains(m_window),
             qPrintable(QStringLiteral("Dragged window was not added to final output 3's layout")));
    QVERIFY(!engine1->windows().contains(m_window));
    if (LayoutEngine *engine2 = manager2->layoutEngine(desktops.at(1))) {
        QVERIFY(!engine2->windows().contains(m_window));
    }

    // Restore the 2-output config for the other tests.
    Test::setOutputConfig({
        Rect(0, 0, 1280, 1024),
        Rect(1280, 0, 1280, 1024),
    });
    VirtualDesktopManager::self()->setPerOutputVirtualDesktops(false);
}

void TilingControllerTest::testMinimizeRemovesFromTiling()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // Two tiled windows on desktop 1.
    m_surface = Test::createSurface();
    m_shellSurface = Test::createXdgToplevelSurface(m_surface.get());
    m_window = Test::renderAndWaitForShown(m_surface.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    m_surface2 = Test::createSurface();
    m_shellSurface2 = Test::createXdgToplevelSurface(m_surface2.get());
    m_window2 = Test::renderAndWaitForShown(m_surface2.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window2);
    QCOMPARE(m_window2->tilingState().mode, TilingState::Mode::Tiled);
    QVERIFY(m_window != m_window2);

    LayoutEngine *engine = m_tileManager->layoutEngine(m_desktop1);
    QVERIFY(engine);
    QVERIFY(engine->windows().contains(m_window));
    QVERIFY(engine->windows().contains(m_window2));
    QCOMPARE(engine->windows().size(), 2);

    // Minimize the first window. The bug: it stayed in the layout and
    // left a "ghost" slot. The fix: it detaches so the layout reflows.
    QSignalSpy minimizedSpy(m_window, &Window::minimizedChanged);
    m_window->setMinimized(true);
    QCOMPARE(minimizedSpy.count(), 1);
    QVERIFY(m_window->isMinimized());

    // The minimized window must no longer be in any engine (no ghost).
    QVERIFY(!engine->windows().contains(m_window));
    QCOMPARE(engine->windows().size(), 1);
    QVERIFY(engine->windows().contains(m_window2));

    // tilingState().mode is intentionally left as Tiled so the window
    // re-joins the layout on unminimize.
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);
}

void TilingControllerTest::testUnminimizeRejoinsTiling()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // Two tiled windows on desktop 1.
    m_surface = Test::createSurface();
    m_shellSurface = Test::createXdgToplevelSurface(m_surface.get());
    m_window = Test::renderAndWaitForShown(m_surface.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    m_surface2 = Test::createSurface();
    m_shellSurface2 = Test::createXdgToplevelSurface(m_surface2.get());
    m_window2 = Test::renderAndWaitForShown(m_surface2.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window2);
    QCOMPARE(m_window2->tilingState().mode, TilingState::Mode::Tiled);
    QVERIFY(m_window != m_window2);

    LayoutEngine *engine = m_tileManager->layoutEngine(m_desktop1);
    QVERIFY(engine);
    QCOMPARE(engine->windows().size(), 2);

    // Minimize the first window: it detaches and the layout reflows.
    m_window->setMinimized(true);
    QVERIFY(m_window->isMinimized());
    QVERIFY(!engine->windows().contains(m_window));
    QCOMPARE(engine->windows().size(), 1);

    // Unminimize: the window must re-join the tiling layout (not float).
    QSignalSpy minimizedSpy(m_window, &Window::minimizedChanged);
    m_window->setMinimized(false);
    QCOMPARE(minimizedSpy.count(), 1);
    QVERIFY(!m_window->isMinimized());

    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);
    QVERIFY2(engine->windows().contains(m_window),
             qPrintable(QStringLiteral("m_window did not re-join engine on unminimize; engine has %1 windows")
                        .arg(engine->windows().size())));
    QCOMPARE(engine->windows().size(), 2);
}


// Helper: ack pending xdg configures until the window reaches the requested
// maximize state. xdg-shell applies state asynchronously on ack, and tiling
// may interleave extra configures, so we drain them in a bounded loop.
static void ackUntilMaximize(Window *window, KWayland::Client::Surface *surface,
                             Test::XdgToplevel *shellSurface, MaximizeMode target)
{
    QSignalSpy surfaceConfigureSpy(shellSurface->xdgSurface(), &Test::XdgSurface::configureRequested);
    QSignalSpy toplevelConfigureSpy(shellSurface, &Test::XdgToplevel::configureRequested);
    for (int i = 0; i < 20 && window->maximizeMode() != target; ++i) {
        if (!surfaceConfigureSpy.wait(2000)) {
            break;
        }
        shellSurface->xdgSurface()->ack_configure(surfaceConfigureSpy.last().at(0).value<quint32>());
        const QSize sz = toplevelConfigureSpy.last().at(0).toSize();
        Test::render(surface, sz.isValid() ? sz : QSize(400, 300), Qt::blue);
        Test::flushWaylandConnection();
    }
}

void TilingControllerTest::testMaximizeReleasesFromTiling()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // Two tiled windows on desktop 1.
    m_surface = Test::createSurface();
    m_shellSurface = Test::createXdgToplevelSurface(m_surface.get());
    m_window = Test::renderAndWaitForShown(m_surface.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    m_surface2 = Test::createSurface();
    m_shellSurface2 = Test::createXdgToplevelSurface(m_surface2.get());
    m_window2 = Test::renderAndWaitForShown(m_surface2.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window2);
    QCOMPARE(m_window2->tilingState().mode, TilingState::Mode::Tiled);
    QVERIFY(m_window != m_window2);

    LayoutEngine *engine = m_tileManager->layoutEngine(m_desktop1);
    QVERIFY(engine);
    QVERIFY(engine->windows().contains(m_window));
    QVERIFY(engine->windows().contains(m_window2));
    QCOMPARE(engine->windows().size(), 2);

    // Maximize the first window. maximize() -> exitQuickTileMode() detaches
    // the window from our leaf synchronously (geometry-safe); maximizedChanged
    // (on ack) triggers pruneEmptyLeaves which destroys the orphaned empty
    // leaf and reflows. Without the fix the empty leaf lingers as a "ghost".
    m_window->setMaximize(true, true);
    QCOMPARE(m_window->requestedMaximizeMode(), MaximizeFull);
    ackUntilMaximize(m_window, m_surface.get(), m_shellSurface.get(), MaximizeFull);
    QCOMPARE(m_window->maximizeMode(), MaximizeFull);

    // The maximized window must no longer be in any engine (no ghost).
    QVERIFY(!engine->windows().contains(m_window));
    QCOMPARE(engine->windows().size(), 1);
    QVERIFY(engine->windows().contains(m_window2));

    // tilingState().mode is intentionally left as Tiled so the window
    // re-joins the layout on unmaximize.
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    // The maximized window should be at full-screen geometry, NOT its old
    // tile geometry (its leaf was released, geometry not clobbered).
    const RectF screen = m_output->geometry();
    QCOMPARE(m_window->frameGeometry(), screen);
}

void TilingControllerTest::testUnmaximizeRejoinsTiling()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // Two tiled windows on desktop 1.
    m_surface = Test::createSurface();
    m_shellSurface = Test::createXdgToplevelSurface(m_surface.get());
    m_window = Test::renderAndWaitForShown(m_surface.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    m_surface2 = Test::createSurface();
    m_shellSurface2 = Test::createXdgToplevelSurface(m_surface2.get());
    m_window2 = Test::renderAndWaitForShown(m_surface2.get(), QSize(400, 300), Qt::blue);
    QVERIFY(m_window2);
    QCOMPARE(m_window2->tilingState().mode, TilingState::Mode::Tiled);
    QVERIFY(m_window != m_window2);

    LayoutEngine *engine = m_tileManager->layoutEngine(m_desktop1);
    QVERIFY(engine);
    QCOMPARE(engine->windows().size(), 2);

    // --- Maximize the first window (drain acks until applied) ---
    m_window->setMaximize(true, true);
    ackUntilMaximize(m_window, m_surface.get(), m_shellSurface.get(), MaximizeFull);
    QCOMPARE(m_window->maximizeMode(), MaximizeFull);
    QVERIFY(!engine->windows().contains(m_window));
    QCOMPARE(engine->windows().size(), 1);

    // --- Unmaximize (drain acks until restored) ---
    m_window->setMaximize(false, false);
    QCOMPARE(m_window->requestedMaximizeMode(), MaximizeRestore);
    ackUntilMaximize(m_window, m_surface.get(), m_shellSurface.get(), MaximizeRestore);
    QCOMPARE(m_window->maximizeMode(), MaximizeRestore);

    // The window must re-join the tiling layout (not float).
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);
    QVERIFY2(engine->windows().contains(m_window),
             qPrintable(QStringLiteral("m_window did not re-join engine on unmaximize; engine has %1 windows")
                        .arg(engine->windows().size())));
    QCOMPARE(engine->windows().size(), 2);
}
} // namespace KWin

WAYLANDTEST_MAIN(KWin::TilingControllerTest)
#include "tiling_controller_test.moc"
