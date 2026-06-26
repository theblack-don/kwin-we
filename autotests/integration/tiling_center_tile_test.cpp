/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "core/output.h"
#include "pointer_input.h"
#include "tiles/centertilelayoutengine.h"
#include "tiles/customtile.h"
#include "tiles/layoutengine.h"
#include "tiles/masterstacklayoutengine.h"
#include "tiles/stackedlayoutengine.h"
#include "tiles/tilemanager.h"
#include "tiling/tilingcontroller.h"
#include "tiling/tilingstate.h"
#include "virtualdesktops.h"
#include "wayland_server.h"
#include "window.h"
#include "workspace.h"

namespace KWin
{

/**
 * Integration tests for the CenterTile layout engine.
 *
 * The layout puts the master (focused) windows in a centred column and any
 * extra windows form two stacks, one on each side.
 *
 *   N <= masterSize (default 1)
 *       All windows stacked vertically in the centre column. Side stacks
 *       have zero width.
 *   N == masterSize + 1
 *       Centre master column (masterSize windows) + a single side stack on
 *       the right (or left, if the extra window is on the left).
 *   N > masterSize + 1
 *       Three columns: left stack, centre master column, right stack.
 *
 * The tests verify:
 *   - The layout is created when setLayout(CenterTile) is invoked.
 *   - The cycleLayout() shortcut picks up the new layout kind.
 *   - The layout's geometry per window matches the documented case rules.
 *   - Per-tile resize (vertical) adjusts a leaf's weight.
 *   - Per-tile resize (horizontal) adjusts masterRatio.
 *   - Minimize/maximize/move-between-monitors/floating all still work.
 */
class TilingCenterTileTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testLayoutKindRegistered();
    void testSetLayoutCreatesEngine();
    void testCycleLayoutIncludesCenterTile();
    void testSingleWindowTakesFullScreen();
    void testTwoWindowsUseCenterColumn();
    void testThreeWindowsUseCenterAndRight();
    void testFourWindowsUseThreeColumns();
    void testHorizontalResizeAdjustsMasterRatio();
    void testVerticalResizeAdjustsLeafWeight();
    void testMinimizeDetachesFromLayout();
    void testMaximizeDetachesFromLayout();
    void testMoveBetweenOutputs();
    void testToggleFloating();

private:
    void createTiledWindow(std::unique_ptr<KWayland::Client::Surface> *surface,
                           std::unique_ptr<Test::XdgToplevel> *shellSurface,
                           Window **window);

    std::unique_ptr<KWayland::Client::Surface> m_surface;
    std::unique_ptr<KWayland::Client::Surface> m_surface2;
    std::unique_ptr<KWayland::Client::Surface> m_surface3;
    std::unique_ptr<KWayland::Client::Surface> m_surface4;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface2;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface3;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface4;
    Window *m_window = nullptr;
    Window *m_window2 = nullptr;
    Window *m_window3 = nullptr;
    Window *m_window4 = nullptr;

    LogicalOutput *m_output = nullptr;
    TileManager *m_tileManager = nullptr;
    VirtualDesktop *m_desktop = nullptr;
};

void TilingCenterTileTest::initTestCase()
{
    qRegisterMetaType<KWin::Window *>();
    QVERIFY(waylandServer()->init(qAppName()));

    kwinApp()->start();
    Test::setOutputConfig({
        Rect(0, 0, 1280, 1024),
    });
    setenv("QT_QPA_PLATFORM", "wayland", true);
}

void TilingCenterTileTest::init()
{
    QVERIFY(Test::setupWaylandConnection());

    workspace()->setActiveOutput(QPoint(640, 512));
    input()->pointer()->warp(QPoint(640, 512));

    m_output = workspace()->activeOutput();
    m_tileManager = workspace()->tileManager(m_output);
    QVERIFY(m_tileManager);

    VirtualDesktopManager::self()->setCount(1);
    m_desktop = VirtualDesktopManager::self()->desktops().at(0);
    VirtualDesktopManager::self()->setCurrent(m_desktop);

    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);
    // Force the active engine to be CenterTile. setLayout is idempotent and
    // migrateable, so this is safe even with existing windows.
    controller->setLayout(LayoutEngine::LayoutKind::CenterTile);
}

void TilingCenterTileTest::cleanup()
{
    m_surface.reset();
    m_surface2.reset();
    m_surface3.reset();
    m_surface4.reset();
    m_shellSurface.reset();
    m_shellSurface2.reset();
    m_shellSurface3.reset();
    m_shellSurface4.reset();
    m_window = nullptr;
    m_window2 = nullptr;
    m_window3 = nullptr;
    m_window4 = nullptr;
}

void TilingCenterTileTest::createTiledWindow(std::unique_ptr<KWayland::Client::Surface> *surface,
                                             std::unique_ptr<Test::XdgToplevel> *shellSurface,
                                             Window **window)
{
    *surface = Test::createSurface();
    QVERIFY(*surface);
    *shellSurface = Test::createXdgToplevelSurface(surface->get());
    QVERIFY(*shellSurface);

    QSignalSpy toplevelConfigureRequestedSpy(shellSurface->get(), &Test::XdgToplevel::configureRequested);
    QSignalSpy surfaceConfigureRequestedSpy(shellSurface->get()->xdgSurface(), &Test::XdgSurface::configureRequested);
    QVERIFY(surfaceConfigureRequestedSpy.wait());

    Test::render(surface->get(), toplevelConfigureRequestedSpy.last().first().value<QSize>(), Qt::blue);
    QVERIFY(Test::waylandSync());

    QSignalSpy windowAddedSpy(workspace(), &Workspace::windowAdded);
    QVERIFY(windowAddedSpy.wait());
    *window = windowAddedSpy.last().first().value<Window *>();
    QVERIFY(*window);

    // Wait for the window to be actually tiled by the controller.
    QTRY_VERIFY((*window)->tilingState().mode == TilingState::Mode::Tiled);
}

void TilingCenterTileTest::testLayoutKindRegistered()
{
    // Round-trip the new LayoutKind through the string helpers used by the
    // KCM and the cycleLayout code path.
    const QString name = LayoutEngine::layoutKindToString(LayoutEngine::LayoutKind::CenterTile);
    QCOMPARE(name, QStringLiteral("CenterTile"));
    QCOMPARE(LayoutEngine::layoutKindFromString(QStringLiteral("CenterTile")),
             LayoutEngine::LayoutKind::CenterTile);
}

void TilingCenterTileTest::testSetLayoutCreatesEngine()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    controller->setLayout(LayoutEngine::LayoutKind::CenterTile);
    LayoutEngine *engine = m_tileManager->layoutEngine(m_desktop);
    QVERIFY(engine);
    QCOMPARE(engine->layoutKind(), LayoutEngine::LayoutKind::CenterTile);
}

void TilingCenterTileTest::testCycleLayoutIncludesCenterTile()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // If we start on MasterStack, one cycle should land on Stacked and the
    // next on CenterTile. This implicitly verifies that CenterTile is in
    // the enabled-layout whitelist (otherwise the cycle would skip it).
    controller->setLayout(LayoutEngine::LayoutKind::MasterStack);
    controller->cycleLayout();
    QCOMPARE(m_tileManager->layoutEngine(m_desktop)->layoutKind(), LayoutEngine::LayoutKind::Stacked);
    controller->cycleLayout();
    QCOMPARE(m_tileManager->layoutEngine(m_desktop)->layoutKind(), LayoutEngine::LayoutKind::CenterTile);
    controller->cycleLayout();
    QCOMPARE(m_tileManager->layoutEngine(m_desktop)->layoutKind(), LayoutEngine::LayoutKind::MasterStack);
}

void TilingCenterTileTest::testSingleWindowTakesFullScreen()
{
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    QVERIFY(m_window);

    const QRectF g = m_window->moveResizeGeometry();
    // The single window should occupy the full output area (within rounding).
    QVERIFY2(qFuzzyCompare(g.width(), qreal(1280)),
             qPrintable(QStringLiteral("Single-window width %1 != 1280").arg(g.width())));
    QVERIFY2(qFuzzyCompare(g.height(), qreal(1024)),
             qPrintable(QStringLiteral("Single-window height %1 != 1024").arg(g.height())));
}

void TilingCenterTileTest::testTwoWindowsUseCenterColumn()
{
    // With masterSize=1, 2 windows: left stack = 1, master column = 1, right
    // stack = 0. The two windows therefore share the centre column vertically.
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    QVERIFY(m_window && m_window2);

    const QRectF g1 = m_window->moveResizeGeometry();
    const QRectF g2 = m_window2->moveResizeGeometry();
    // Both windows are in the centre column with masterRatio=0.6 and there
    // are no side stacks, so the centre column takes the full width.
    QVERIFY2(qFuzzyCompare(g1.width(), qreal(1280)),
             qPrintable(QStringLiteral("Window 1 width %1 != 1280").arg(g1.width())));
    QVERIFY2(qFuzzyCompare(g2.width(), qreal(1280)),
             qPrintable(QStringLiteral("Window 2 width %1 != 1280").arg(g2.width())));
    // They split the height in half.
    QVERIFY2(qFuzzyCompare(g1.height() + g2.height(), qreal(1024)),
             qPrintable(QStringLiteral("Two-window heights don't add up: %1 + %2 != 1024")
                            .arg(g1.height()).arg(g2.height())));
}

void TilingCenterTileTest::testThreeWindowsUseCenterAndRight()
{
    // With masterSize=1, 3 windows: left stack = 1, master = 1, right = 1.
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);

    const QRectF g1 = m_window->moveResizeGeometry();
    const QRectF g2 = m_window2->moveResizeGeometry();
    const QRectF g3 = m_window3->moveResizeGeometry();
    // Three columns: left + master + right = 1280.
    QVERIFY2(qFuzzyCompare(g1.width() + g2.width() + g3.width(), qreal(1280)),
             qPrintable(QStringLiteral("Three widths don't add up: %1+%2+%3 != 1280")
                            .arg(g1.width()).arg(g2.width()).arg(g3.width())));
    // The centre column is wider than each side column (default ratio 0.6).
    QVERIFY2(g2.width() > g1.width(),
             qPrintable(QStringLiteral("Centre column %1 not wider than left %2").arg(g2.width()).arg(g1.width())));
    QVERIFY2(g2.width() > g3.width(),
             qPrintable(QStringLiteral("Centre column %1 not wider than right %2").arg(g2.width()).arg(g3.width())));
    // Left == right (symmetry).
    QVERIFY2(qFuzzyCompare(g1.width(), g3.width()),
             qPrintable(QStringLiteral("Left %1 != right %2").arg(g1.width()).arg(g3.width())));
}

void TilingCenterTileTest::testFourWindowsUseThreeColumns()
{
    // With masterSize=1, 4 windows: left stack = 2, master = 1, right = 1.
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    createTiledWindow(&m_surface4, &m_shellSurface4, &m_window4);
    QVERIFY(m_window && m_window2 && m_window3 && m_window4);

    const QRectF g1 = m_window->moveResizeGeometry();
    const QRectF g2 = m_window2->moveResizeGeometry();
    const QRectF g3 = m_window3->moveResizeGeometry();
    const QRectF g4 = m_window4->moveResizeGeometry();
    // Three columns of width masterRatio / 2*(1-masterRatio) / 2*(1-masterRatio).
    QVERIFY2(qFuzzyCompare(g1.width() + g2.width() + g4.width(), qreal(1280)),
             qPrintable(QStringLiteral("Four windows: widths don't add up: %1+%2+%4 != 1280")
                            .arg(g1.width()).arg(g2.width()).arg(g4.width())));
    QVERIFY2(g2.width() > g1.width(),
             qPrintable(QStringLiteral("Centre %1 not wider than left %2").arg(g2.width()).arg(g1.width())));
    QVERIFY2(g2.width() > g4.width(),
             qPrintable(QStringLiteral("Centre %1 not wider than right %2").arg(g2.width()).arg(g4.width())));
    QVERIFY2(qFuzzyCompare(g1.width(), g4.width()),
             qPrintable(QStringLiteral("Left %1 != right %2").arg(g1.width()).arg(g4.width())));
    // Window 3 is in the left stack (it shares x with window 1, not window 2).
    QCOMPARE(g1.x(), g3.x());
    // Window 4 is alone in the right stack.
    QVERIFY(g4.x() > g2.x());
}

void TilingCenterTileTest::testHorizontalResizeAdjustsMasterRatio()
{
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);

    CenterTileLayoutEngine *engine = qobject_cast<CenterTileLayoutEngine *>(
        m_tileManager->layoutEngine(m_desktop));
    QVERIFY(engine);

    const qreal ratioBefore = engine->masterRatio();
    const qreal widthBefore = m_window2->moveResizeGeometry().width(); // the master

    workspace()->tilingController()->growActiveTileSize(Qt::Horizontal);
    const qreal ratioAfter = engine->masterRatio();
    const qreal widthAfter = m_window2->moveResizeGeometry().width();
    QVERIFY2(ratioAfter > ratioBefore,
             qPrintable(QStringLiteral("masterRatio did not grow: %1 -> %2").arg(ratioBefore).arg(ratioAfter)));
    QVERIFY2(widthAfter > widthBefore,
             qPrintable(QStringLiteral("Master width did not grow: %1 -> %2").arg(widthBefore).arg(widthAfter)));
}

void TilingCenterTileTest::testVerticalResizeAdjustsLeafWeight()
{
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    QVERIFY(m_window && m_window2);

    workspace()->activateWindow(m_window);
    const qreal h1Before = m_window->moveResizeGeometry().height();
    const qreal h2Before = m_window2->moveResizeGeometry().height();

    workspace()->tilingController()->growActiveTileSize(Qt::Vertical);
    const qreal h1After = m_window->moveResizeGeometry().height();
    const qreal h2After = m_window2->moveResizeGeometry().height();

    QVERIFY2(h1After > h1Before,
             qPrintable(QStringLiteral("Window 1 (active) did not grow: %1 -> %2").arg(h1Before).arg(h1After)));
    QVERIFY2(h2After < h2Before,
             qPrintable(QStringLiteral("Window 2 (sibling) did not shrink: %1 -> %2").arg(h2Before).arg(h2After)));
}

void TilingCenterTileTest::testMinimizeDetachesFromLayout()
{
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    QVERIFY(m_window && m_window2);

    // Minimize should detach from the layout, leaving the other window
    // taking the full output area.
    QSignalSpy minSpy(m_window, &Window::minimizedChanged);
    m_window->setMinimized(true);
    QVERIFY(minSpy.wait());

    QTRY_VERIFY(m_window->tilingState().mode == TilingState::Mode::Floating);
    // The remaining window should now occupy the full screen.
    QTRY_VERIFY(qFuzzyCompare(m_window2->moveResizeGeometry().width(), qreal(1280))
                && qFuzzyCompare(m_window2->moveResizeGeometry().height(), qreal(1024)));
}

void TilingCenterTileTest::testMaximizeDetachesFromLayout()
{
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    QVERIFY(m_window && m_window2);

    // Maximize should detach from the layout, leaving the other window
    // taking the full output area.
    QSignalSpy maxSpy(m_window, &Window::maximizedChanged);
    m_window->maximize(MaximizeFull);
    QVERIFY(maxSpy.wait());

    QTRY_VERIFY(m_window->tilingState().mode == TilingState::Mode::Floating);
    QTRY_VERIFY(qFuzzyCompare(m_window2->moveResizeGeometry().width(), qreal(1280))
                && qFuzzyCompare(m_window2->moveResizeGeometry().height(), qreal(1024)));
}

void TilingCenterTileTest::testMoveBetweenOutputs()
{
    // Add a second output to verify monitor-to-monitor moves work.
    Test::setOutputConfig({
        Rect(0, 0, 1280, 1024),
        Rect(1280, 0, 1280, 1024),
    });
    QTRY_COMPARE(workspace()->outputs().size(), 2);

    LogicalOutput *otherOutput = nullptr;
    for (LogicalOutput *out : workspace()->outputs()) {
        if (out != m_output) {
            otherOutput = out;
            break;
        }
    }
    QVERIFY(otherOutput);

    TilingController *controller = workspace()->tilingController();
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    QVERIFY(m_window);

    // Force the destination output to use the CenterTile layout by switching
    // to that output first (which routes through setLayout()).
    workspace()->setActiveOutput(QPoint(1280 + 640, 512));
    controller->setLayout(LayoutEngine::LayoutKind::CenterTile);
    // Switch back to the source output so the move call below picks the right
    // pair of outputs.
    workspace()->setActiveOutput(QPoint(640, 512));

    QSignalSpy outputChangedSpy(m_window, &Window::outputChanged);
    QVERIFY(outputChangedSpy.isValid());
    controller->moveWindowToOutput(TilingController::TilingDirection::East);
    QVERIFY(outputChangedSpy.wait());
    QCOMPARE(m_window->output(), otherOutput);

    // The window should now be managed by the destination output's engine.
    QTRY_VERIFY(m_window->tilingState().mode == TilingState::Mode::Tiled);
}

void TilingCenterTileTest::testToggleFloating()
{
    TilingController *controller = workspace()->tilingController();
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    QVERIFY(m_window && m_window2);

    QVERIFY(m_window->tilingState().mode == TilingState::Mode::Tiled);

    controller->toggleFloating();
    QTRY_VERIFY(m_window->tilingState().mode == TilingState::Mode::Floating);

    controller->toggleFloating();
    QTRY_VERIFY(m_window->tilingState().mode == TilingState::Mode::Tiled);
}

} // namespace KWin

WAYLANDTEST_MAIN(KWin::TilingCenterTileTest)
#include "tiling_center_tile_test.moc"
