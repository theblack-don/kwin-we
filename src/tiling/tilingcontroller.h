/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwin_export.h"
#include "tilingrules.h"
#include "tilingstate.h"
#include "tiles/layoutengine.h"

#include <KConfigWatcher>
#include <KSharedConfig>

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

class KConfigGroup;

namespace KWin
{

class LayoutEngine;
class LogicalOutput;
class TileManager;
class VirtualDesktop;
class Window;
class Workspace;

/**
 * Singleton controller that manages the native tiling state.
 *
 * It decides which windows tile, reacts to window creation/destruction,
 * and handles keyboard-driven tiling commands.
 */
class KWIN_EXPORT TilingController : public QObject
{
    Q_OBJECT

public:
    explicit TilingController(Workspace *workspace);
    ~TilingController() override;

    /**
     * Called by Workspace when a window is added.
     */
    void onWindowAdded(Window *window);

    /**
     * Called by Workspace when a window is removed.
     */
    void onWindowRemoved(Window *window);

    /**
     * Re-read configuration from kwinrc.
     */
    void reconfigure();

    /**
     * Initialize default layout engines for all existing outputs/desktops.
     */
    void initializeLayouts();

    /**
     * Initialize default layout engines for a newly added output.
     */
    void onOutputAdded(LogicalOutput *output);

    enum class TilingDirection {
        West,
        East,
    };

    // Keyboard actions
    void focusLeft();
    void focusRight();
    void focusUp();
    void focusDown();
    void toggleFloating();
    void promoteToMaster();
    void moveWindowToOutput(TilingDirection direction);
    void moveWindowNext();
    void moveWindowPrevious();

    /**
     * Per-tile resize actions. Routes to the active window's layout engine
     * and asks it to grow (@p grow == true) or shrink the active leaf along
     * the given @p axis. The delta is m_resizeStep (configurable via kwinrc).
     * No-op if there is no active tiled window or the engine does not
     * support per-tile resize.
     */
    void growActiveTileSize(Qt::Orientation axis);
    void shrinkActiveTileSize(Qt::Orientation axis);

    /**
     * Current per-tile resize step. Exposed for tests, scripts, and the KCM
     * to display. Always within (0, 1].
     */
    qreal resizeStep() const { return m_resizeStep; }

    /**
     * Mouse-driven resize plumbing. Called from onInteractiveMoveResizeStarted
     * when the user grabs a resize handle on a tiled window. Captures the
     * source leaf and the edge being dragged so the corresponding end of
     * endInteractiveResize can apply a weight delta to the engine.
     */
    void beginInteractiveResize(Window *window);
    /**
     * Applies the resize. @p finalGeometry is the window's geometry at the
     * moment the user released the mouse. The pixel delta vs. the
     * pre-resize geometry is translated into a weight delta on the
     * appropriate engine. @p edge is the edge the user grabbed.
     */
    void endInteractiveResize(Window *window, const RectF &finalGeometry, Qt::Edge edge);

    /**
     * Switch the layout of the active monitor's current desktop to @p kind,
     * rebuilding the engine in place and re-adding the windows it was
     * managing so the visual change is immediate.
     */
    void setLayout(LayoutEngine::LayoutKind kind);

    /**
     * Cycle the active monitor's current desktop through the list of layouts
     * currently enabled in kwinrc.
     */
    void cycleLayout();

    TilingRules *rules() const { return m_rules.get(); }

    enum class BorderMode {
        None,
        AllTiled,
        ActiveOnly,
        AllWindows,
    };

private Q_SLOTS:
    void onInteractiveMoveResizeStarted();
    void onInteractiveMoveResizeFinished();
    void onWindowDesktopsChanged(Window *window);
    void onWindowMinimizedChanged(Window *window);
    void onWindowMaximizedChanged(Window *window);
    // Called when the noctalia colors file (or kdeglobals) reports a
    // change. Debounces the actual re-read so we only re-resolve colors
    // once per batch of file events.
    void onColorSourcesChanged();

private:
    bool shouldTile(const Window *window) const;
    void onWindowMoveFinished(Window *window);
    void focusInDirection(LayoutEngine::FocusDirection direction);
    Window *windowUnderCursorInEngine(LayoutEngine *engine) const;
    void addWindowToLayout(Window *window, LogicalOutput *output, VirtualDesktop *desktop);
    void removeWindowFromLayouts(Window *window);
    void migrateWindow(Window *window, LogicalOutput *newOutput, VirtualDesktop *newDesktop);
    LayoutEngine *activeLayoutEngine() const;
    LayoutEngine *layoutEngineForWindow(Window *window, LogicalOutput **output = nullptr, VirtualDesktop **desktop = nullptr) const;
    Window *activeTiledWindow() const;

    void setupLayoutEngine(TileManager *manager, VirtualDesktop *desktop, LayoutEngine::LayoutKind kind);
    LayoutEngine::LayoutKind resolveLayoutKind(LogicalOutput *output) const;
    LayoutEngine::LayoutKind globalDefaultLayoutKind() const;
    QList<LayoutEngine::LayoutKind> enabledLayoutKinds() const;
    bool isLayoutEnabled(LayoutEngine::LayoutKind kind) const;
    void applyGapSettingsToOutput(LogicalOutput *output);
    void updateBorders();

    void setLayoutOn(LogicalOutput *output, VirtualDesktop *desktop, LayoutEngine::LayoutKind kind);
    void reconcileLayoutKinds();

    QPointer<Workspace> m_workspace;
    std::unique_ptr<TilingRules> m_rules;
    bool m_enabled = true;
    LayoutEngine::LayoutKind m_defaultLayout = LayoutEngine::LayoutKind::MasterStack;
    // Per-tile resize step. Units: weight (master/stack column ratio) for
    // horizontal axis on a master/stack layout, otherwise a per-leaf weight
    // delta. Clamped to a sane range in reconfigure().
    qreal m_resizeStep = 0.1;
    QStringList m_enabledLayouts;
    BorderMode m_borderMode = BorderMode::None;
    qreal m_borderThickness = 2.0;
    int m_cornerRadius = 0;
    enum class ColorSource {
        Custom,
        SystemAccent,
        SystemAccentFaded,
        NoctaliaPrimary,
        NoctaliaAccent,
    };
    ColorSource m_colorSourceActive = ColorSource::SystemAccent;
    ColorSource m_colorSourceInactive = ColorSource::SystemAccentFaded;
    QColor m_borderColorActive;
    QColor m_borderColorInactive;

    QColor resolveColor(ColorSource source, const QColor &custom) const;
    void applyCornerRadius(Window *window);

    void readNoctaliaColors();
    void readSystemAccent();
    bool usesNoctaliaSource() const;

    QColor m_noctaliaPrimaryColor;
    QColor m_noctaliaAccentColor;
    QColor m_cachedSystemAccent;
    // KSharedConfig instances are cached per-name. We hold long-lived
    // references so we can call reparseConfiguration() when the underlying
    // file changes (via KConfigWatcher), and so all reads see fresh data.
    KSharedConfigPtr m_noctaliaConfig;
    KSharedConfigPtr m_kdeglobalsConfig;
    KConfigWatcher::Ptr m_noctaliaWatcher;
    // QFileSystemWatcher is used for the noctalia colors file because
    // KConfigWatcher silently refuses to watch absolute paths, and
    // QStandardPaths::locate() returns an absolute path.
    QFileSystemWatcher *m_noctaliaFsWatcher = nullptr;
    QString m_noctaliaPath;
    // Debounce the color re-reads: editors and shell-integration daemons
    // often rewrite the file in two steps (write tmp + rename), which
    // produces multiple fileChanged events in quick succession.
    QTimer m_colorReloadTimer;
    KConfigWatcher::Ptr m_kdeglobalsWatcher;

    struct MoveContext {
        QPointer<LayoutEngine> engine;
        QPointer<LogicalOutput> output;
        RectF originalGeometryRestore;
    };
    QHash<Window *, MoveContext> m_activeMoves;

    struct ResizeContext {
        QPointer<LayoutEngine> engine;
        RectF originalGeometry;
        Qt::Edge edge = Qt::RightEdge;
    };
    QHash<Window *, ResizeContext> m_activeResizes;
};

} // namespace KWin
