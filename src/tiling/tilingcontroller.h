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
#include <QHash>
#include <QObject>
#include <QPointer>

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
    };

private Q_SLOTS:
    void onInteractiveMoveResizeStarted();
    void onInteractiveMoveResizeFinished();
    void onWindowDesktopsChanged(Window *window);

private:
    bool shouldTile(const Window *window) const;
    void onWindowMoveFinished(Window *window);
    void focusInDirection(LayoutEngine::FocusDirection direction);
    Window *windowUnderCursorInEngine(LayoutEngine *engine) const;
    void addWindowToLayout(Window *window, LogicalOutput *output, VirtualDesktop *desktop);
    void removeWindowFromLayouts(Window *window);
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
    KConfigWatcher::Ptr m_kdeglobalsWatcher;

    struct MoveContext {
        QPointer<LayoutEngine> engine;
        QPointer<LogicalOutput> output;
        RectF originalGeometryRestore;
    };
    QHash<Window *, MoveContext> m_activeMoves;
};

} // namespace KWin
