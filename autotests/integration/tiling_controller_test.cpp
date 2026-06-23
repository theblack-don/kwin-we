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

} // namespace KWin

WAYLANDTEST_MAIN(KWin::TilingControllerTest)
#include "tiling_controller_test.moc"
