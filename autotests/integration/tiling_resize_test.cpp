/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "core/output.h"
#include "input.h"
#include "pointer_input.h"
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
 * Integration tests for the per-tile resize plumbing.
 *
 * The feature under test:
 *  - TilingController::growActiveTileSize / shrinkActiveTileSize adjust a
 *    tiled window's share of its column, and (in master/stack) the column
 *    divider itself. Other windows in the same column/area shrink to make
 *    room, keeping the layout valid.
 *  - Both MasterStackLayoutEngine and StackedLayoutEngine implement the
 *    supportsPerTileResize() / adjustTileSize() interface.
 *  - Single-column engines (Stacked) ignore horizontal axis requests.
 */
class TilingResizeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMasterStackGrowHorizontalGrowsMasterRatio();
    void testMasterStackShrinkHorizontalShrinksMasterRatio();
    void testMasterStackGrowVerticalAdjustsLeafWeight();
    void testStackedGrowVerticalAdjustsLeafWeight();
    void testStackedHorizontalIsNoOp();
    void testGrowIsNoOpForFloatingWindow();

private:
    std::unique_ptr<KWayland::Client::Surface> m_surface;
    std::unique_ptr<KWayland::Client::Surface> m_surface2;
    std::unique_ptr<KWayland::Client::Surface> m_surface3;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface2;
    std::unique_ptr<Test::XdgToplevel> m_shellSurface3;
    Window *m_window = nullptr;
    Window *m_window2 = nullptr;
    Window *m_window3 = nullptr;

    LogicalOutput *m_output = nullptr;
    TileManager *m_tileManager = nullptr;
    VirtualDesktop *m_desktop = nullptr;
};

void TilingResizeTest::initTestCase()
{
    qRegisterMetaType<KWin::Window *>();
    QVERIFY(waylandServer()->init(qAppName()));

    kwinApp()->start();
    Test::setOutputConfig({
        Rect(0, 0, 1280, 1024),
    });
    setenv("QT_QPA_PLATFORM", "wayland", true);
}

void TilingResizeTest::init()
{
    QVERIFY(Test::setupWaylandConnection());

    workspace()->setActiveOutput(QPoint(640, 512));
    input()->pointer()->warp(QPoint(640, 512));

    m_output = workspace()->activeOutput();
    m_tileManager = workspace()->tileManager(m_output);
    QVERIFY(m_tileManager);

    // Make sure we have a single, clean desktop and that the default layout
    // is MasterStack (so the first set of tests uses that engine).
    VirtualDesktopManager::self()->setCount(1);
    m_desktop = VirtualDesktopManager::self()->desktops().at(0);
    VirtualDesktopManager::self()->setCurrent(m_desktop);

    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);
    // Force the active engine to be MasterStack. setLayout is idempotent and
    // migrateable, so this is safe even with existing windows.
    controller->setLayout(LayoutEngine::LayoutKind::MasterStack);
}

void TilingResizeTest::cleanup()
{
    m_window = nullptr;
    m_window2 = nullptr;
    m_window3 = nullptr;
    m_shellSurface.reset();
    m_shellSurface2.reset();
    m_shellSurface3.reset();
    m_surface.reset();
    m_surface2.reset();
    m_surface3.reset();
    Test::destroyWaylandConnection();
}

static void createTiledWindow(std::unique_ptr<KWayland::Client::Surface> *surfaceOut,
                              std::unique_ptr<Test::XdgToplevel> *shellSurfaceOut,
                              Window **windowOut)
{
    *surfaceOut = Test::createSurface();
    QVERIFY(*surfaceOut);
    *shellSurfaceOut = Test::createXdgToplevelSurface(surfaceOut->get());
    *windowOut = Test::renderAndWaitForShown(surfaceOut->get(), QSize(400, 300), Qt::blue);
    QVERIFY(*windowOut);
}

void TilingResizeTest::testMasterStackGrowHorizontalGrowsMasterRatio()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    QVERIFY(m_window);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);

    // Make this window the active one so growActiveTileSize targets it.
    workspace()->activateWindow(m_window);

    MasterStackLayoutEngine *engine = qobject_cast<MasterStackLayoutEngine *>(
        m_tileManager->layoutEngine(m_desktop));
    QVERIFY(engine);
    const qreal ratioBefore = engine->masterRatio();

    controller->growActiveTileSize(Qt::Horizontal);

    const qreal ratioAfter = engine->masterRatio();
    QVERIFY2(ratioAfter > ratioBefore,
             qPrintable(QStringLiteral("masterRatio did not grow on horizontal resize: %1 -> %2")
                        .arg(ratioBefore).arg(ratioAfter)));
    QCOMPARE(ratioAfter, ratioBefore + controller->resizeStep());
}

void TilingResizeTest::testMasterStackShrinkHorizontalShrinksMasterRatio()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    QVERIFY(m_window);
    workspace()->activateWindow(m_window);

    MasterStackLayoutEngine *engine = qobject_cast<MasterStackLayoutEngine *>(
        m_tileManager->layoutEngine(m_desktop));
    QVERIFY(engine);
    const qreal ratioBefore = engine->masterRatio();

    controller->shrinkActiveTileSize(Qt::Horizontal);

    const qreal ratioAfter = engine->masterRatio();
    QVERIFY2(ratioAfter < ratioBefore,
             qPrintable(QStringLiteral("masterRatio did not shrink: %1 -> %2")
                        .arg(ratioBefore).arg(ratioAfter)));
    QCOMPARE(ratioAfter, ratioBefore - controller->resizeStep());
}

void TilingResizeTest::testMasterStackGrowVerticalAdjustsLeafWeight()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // Two windows on master + one on stack.
    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);
    QCOMPARE(m_window->tilingState().mode, TilingState::Mode::Tiled);
    QCOMPARE(m_window2->tilingState().mode, TilingState::Mode::Tiled);
    QCOMPARE(m_window3->tilingState().mode, TilingState::Mode::Tiled);

    MasterStackLayoutEngine *engine = qobject_cast<MasterStackLayoutEngine *>(
        m_tileManager->layoutEngine(m_desktop));
    QVERIFY(engine);

    // The third window is in the stack column. Activate it and grow vertically:
    // its leaf should grow, and the master's other stack leaf (none here) or
    // master column would absorb the deficit. With one master + two stack,
    // the stack siblings share the change.
    workspace()->activateWindow(m_window3);
    const QRectF geomBefore = m_window3->moveResizeGeometry();
    const QSizeF sizeBefore = geomBefore.size();

    controller->growActiveTileSize(Qt::Vertical);

    const QRectF geomAfter = m_window3->moveResizeGeometry();
    const QSizeF sizeAfter = geomAfter.size();
    QVERIFY2(sizeAfter.height() > sizeBefore.height(),
             qPrintable(QStringLiteral("Stack window did not grow vertically: %1x%2 -> %3x%4")
                        .arg(sizeBefore.width()).arg(sizeBefore.height())
                        .arg(sizeAfter.width()).arg(sizeAfter.height())));
    // The window must remain tiled (the regression we are guarding against).
    QCOMPARE(m_window3->tilingState().mode, TilingState::Mode::Tiled);
}

void TilingResizeTest::testStackedGrowVerticalAdjustsLeafWeight()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    // Switch the active engine to Stacked.
    controller->setLayout(LayoutEngine::LayoutKind::Stacked);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    createTiledWindow(&m_surface3, &m_shellSurface3, &m_window3);
    QVERIFY(m_window && m_window2 && m_window3);

    StackedLayoutEngine *engine = qobject_cast<StackedLayoutEngine *>(
        m_tileManager->layoutEngine(m_desktop));
    QVERIFY(engine);
    QVERIFY(engine->supportsPerTileResize());

    // Grow the middle window. Its height must grow and a sibling must shrink.
    workspace()->activateWindow(m_window2);
    const qreal h2Before = m_window2->moveResizeGeometry().height();
    const qreal h1Before = m_window->moveResizeGeometry().height();
    const qreal h3Before = m_window3->moveResizeGeometry().height();

    controller->growActiveTileSize(Qt::Vertical);

    const qreal h2After = m_window2->moveResizeGeometry().height();
    const qreal h1After = m_window->moveResizeGeometry().height();
    const qreal h3After = m_window3->moveResizeGeometry().height();

    QVERIFY2(h2After > h2Before,
             qPrintable(QStringLiteral("Stacked window did not grow: %1 -> %2").arg(h2Before).arg(h2After)));
    // At least one sibling must shrink (or both, depending on clamp behaviour).
    QVERIFY2(h1After < h1Before || h3After < h3Before,
             qPrintable(QStringLiteral("Stacked siblings did not shrink: h1 %1->%2, h3 %3->%4")
                        .arg(h1Before).arg(h1After).arg(h3Before).arg(h3After)));
    // The total screen height used must remain constant (within rounding).
    const qreal totalBefore = h1Before + h2Before + h3Before;
    const qreal totalAfter = h1After + h2After + h3After;
    QVERIFY2(qFuzzyCompare(totalBefore, totalAfter),
             qPrintable(QStringLiteral("Total column height changed: %1 -> %2").arg(totalBefore).arg(totalAfter)));
}

void TilingResizeTest::testStackedHorizontalIsNoOp()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    controller->setLayout(LayoutEngine::LayoutKind::Stacked);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    createTiledWindow(&m_surface2, &m_shellSurface2, &m_window2);
    QVERIFY(m_window && m_window2);

    workspace()->activateWindow(m_window);
    const QRectF geomBefore = m_window->moveResizeGeometry();
    const qreal weightBefore = m_window->tilingState().layoutIndex;

    // Horizontal resize on a single-column stack must be a no-op: Stacked
    // has no horizontal axis. The engine's adjustTileSize returns silently
    // for Qt::Horizontal.
    controller->growActiveTileSize(Qt::Horizontal);
    controller->shrinkActiveTileSize(Qt::Horizontal);

    const QRectF geomAfter = m_window->moveResizeGeometry();
    QVERIFY2(geomBefore.size() == geomAfter.size(),
             qPrintable(QStringLiteral("Stacked window width changed on horizontal resize: %1x%2 -> %3x%4")
                        .arg(geomBefore.size().width()).arg(geomBefore.size().height())
                        .arg(geomAfter.size().width()).arg(geomAfter.size().height())));
    Q_UNUSED(weightBefore);
}

void TilingResizeTest::testGrowIsNoOpForFloatingWindow()
{
    TilingController *controller = workspace()->tilingController();
    QVERIFY(controller);

    createTiledWindow(&m_surface, &m_shellSurface, &m_window);
    QVERIFY(m_window);

    // Manually mark the window as floating. The grow/shrink methods must
    // do nothing in that case (activeTiledWindow() returns nullptr).
    m_window->tilingState().mode = TilingState::Mode::Floating;
    m_window->tilingState().userToggledFloat = true;
    workspace()->activateWindow(m_window);

    MasterStackLayoutEngine *engine = qobject_cast<MasterStackLayoutEngine *>(
        m_tileManager->layoutEngine(m_desktop));
    QVERIFY(engine);
    const qreal ratioBefore = engine->masterRatio();

    controller->growActiveTileSize(Qt::Horizontal);
    controller->growActiveTileSize(Qt::Vertical);
    controller->shrinkActiveTileSize(Qt::Horizontal);
    controller->shrinkActiveTileSize(Qt::Vertical);

    QCOMPARE(engine->masterRatio(), ratioBefore);
}

} // namespace KWin

WAYLANDTEST_MAIN(KWin::TilingResizeTest)
#include "tiling_resize_test.moc"
