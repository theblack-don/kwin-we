/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tilingcontroller.h"

#include "core/output.h"
#include "core/rect.h"
#include "cursor.h"
#include "scene/borderradius.h"
#include "tiles/layoutengine.h"
#include "tiles/masterstacklayoutengine.h"
#include "tiles/stackedlayoutengine.h"
#include "tiles/tilemanager.h"
#include "utils/gravity.h"
#include "virtualdesktops.h"
#include "window.h"
#include "workspace.h"

#include <KColorScheme>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QGuiApplication>
#include <QPalette>
#include <QStandardPaths>
#include <QtGlobal>

namespace KWin
{

namespace
{

std::unique_ptr<LayoutEngine> createLayoutEngine(LayoutEngine::LayoutKind kind, QObject *parent)
{
    switch (kind) {
    case LayoutEngine::LayoutKind::Stacked:
        return std::make_unique<StackedLayoutEngine>(parent);
    case LayoutEngine::LayoutKind::MasterStack:
    default:
        return std::make_unique<MasterStackLayoutEngine>(parent);
    }
}

} // namespace

TilingController::TilingController(Workspace *workspace)
    : QObject(workspace)
    , m_workspace(workspace)
    , m_rules(std::make_unique<TilingRules>())
{
    reconfigure();

    if (m_workspace) {
        connect(m_workspace, &Workspace::windowActivated, this, [this]() {
            updateBorders();
        });
    }

    // Hold long-lived KSharedConfig handles for the two config files we
    // tail. KSharedConfig caches parsed values in memory; without
    // reparseConfiguration() the second read after an external edit would
    // see stale data even though the watcher fired.
    const QString noctaliaPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("color-schemes/noctalia.colors"));
    if (!noctaliaPath.isEmpty()) {
        m_noctaliaConfig = KSharedConfig::openConfig(noctaliaPath, KConfig::SimpleConfig);
        m_noctaliaWatcher = KConfigWatcher::create(m_noctaliaConfig);
        connect(m_noctaliaWatcher.data(), &KConfigWatcher::configChanged, this, [this]() {
            readNoctaliaColors();
            if (usesNoctaliaSource()) {
                updateBorders();
            }
        });
    }
    m_kdeglobalsConfig = KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::FullConfig);
    m_kdeglobalsWatcher = KConfigWatcher::create(m_kdeglobalsConfig);
    connect(m_kdeglobalsWatcher.data(), &KConfigWatcher::configChanged, this, [this]() {
        readSystemAccent();
        if (m_colorSourceActive == ColorSource::SystemAccent
            || m_colorSourceInactive == ColorSource::SystemAccent
            || m_colorSourceInactive == ColorSource::SystemAccentFaded) {
            updateBorders();
        }
    });

    // Initial reads so the very first border paint already has the right color.
    readNoctaliaColors();
    readSystemAccent();
}

TilingController::~TilingController() = default;

void TilingController::reconfigure()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(KWIN_CONFIG);
    KConfigGroup tilingGroup(config, QStringLiteral("Tiling"));
    KConfigGroup rulesGroup(config, QStringLiteral("TilingRules"));

    m_enabled = tilingGroup.readEntry("Enabled", true);
    m_defaultLayout = LayoutEngine::layoutKindFromString(
        tilingGroup.readEntry("DefaultLayout", QStringLiteral("MasterStack")));
    // Whitelist of layouts the user wants available. Order in the list also
    // defines the cycle order used by cycleLayout().
    m_enabledLayouts = tilingGroup.readEntry("EnabledLayouts",
        QStringList{QLatin1String("MasterStack"), QLatin1String("Stacked")});
    m_rules->load(rulesGroup);

    // Per-tile resize step. Weight units; see tilingcontroller.h. Clamped to
    // a sane range so a misconfigured kwinrc cannot disable resize or make
    // a single press span the whole column.
    m_resizeStep = std::clamp(tilingGroup.readEntry("ResizeStep", 0.1), qreal(0.01), qreal(1.0));

    const QString borderModeString = tilingGroup.readEntry("TilingBorderMode", QStringLiteral("None"));
    if (borderModeString == QLatin1String("AllTiled")) {
        m_borderMode = BorderMode::AllTiled;
    } else if (borderModeString == QLatin1String("ActiveOnly")) {
        m_borderMode = BorderMode::ActiveOnly;
    } else {
        m_borderMode = BorderMode::None;
    }
    m_borderThickness = tilingGroup.readEntry("TilingBorderThickness", 2.0);
    m_cornerRadius = tilingGroup.readEntry("TilingCornerRadius", 0);

    const QString activeSourceString = tilingGroup.readEntry("TilingBorderColorSourceActive", QStringLiteral("SystemAccent"));
    if (activeSourceString == QLatin1String("Custom")) {
        m_colorSourceActive = ColorSource::Custom;
    } else if (activeSourceString == QLatin1String("NoctaliaPrimary")) {
        m_colorSourceActive = ColorSource::NoctaliaPrimary;
    } else if (activeSourceString == QLatin1String("NoctaliaAccent")) {
        m_colorSourceActive = ColorSource::NoctaliaAccent;
    } else {
        m_colorSourceActive = ColorSource::SystemAccent;
    }
    const QString inactiveSourceString = tilingGroup.readEntry("TilingBorderColorSourceInactive", QStringLiteral("SystemAccentFaded"));
    if (inactiveSourceString == QLatin1String("Custom")) {
        m_colorSourceInactive = ColorSource::Custom;
    } else if (inactiveSourceString == QLatin1String("SystemAccent")) {
        m_colorSourceInactive = ColorSource::SystemAccent;
    } else if (inactiveSourceString == QLatin1String("NoctaliaPrimary")) {
        m_colorSourceInactive = ColorSource::NoctaliaPrimary;
    } else if (inactiveSourceString == QLatin1String("NoctaliaAccent")) {
        m_colorSourceInactive = ColorSource::NoctaliaAccent;
    } else {
        m_colorSourceInactive = ColorSource::SystemAccentFaded;
    }
    m_borderColorActive = tilingGroup.readEntry("TilingBorderColorActive",
        QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight));
    m_borderColorInactive = tilingGroup.readEntry("TilingBorderColorInactive", QColor(128, 128, 128, 200));

    initializeLayouts();
    reconcileLayoutKinds();

    if (m_workspace) {
        for (LogicalOutput *output : m_workspace->outputs()) {
            applyGapSettingsToOutput(output);
        }
        updateBorders();
    }
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
    const LayoutEngine::LayoutKind kind = resolveLayoutKind(output);
    for (VirtualDesktop *desktop : VirtualDesktopManager::self()->desktops()) {
        setupLayoutEngine(manager, desktop, kind);
    }
    applyGapSettingsToOutput(output);
}

void TilingController::setupLayoutEngine(TileManager *manager, VirtualDesktop *desktop, LayoutEngine::LayoutKind kind)
{
    if (!manager || !desktop) {
        return;
    }

    // If a layout engine is already attached, leave it alone. Switching the
    // layout on a live engine is handled by setLayout() / cycleLayout() so
    // that already-tiled windows can be migrated cleanly.
    if (manager->layoutEngine(desktop)) {
        return;
    }

    auto engine = createLayoutEngine(kind, manager);
    manager->setLayoutEngine(desktop, std::move(engine));
}

LayoutEngine::LayoutKind TilingController::globalDefaultLayoutKind() const
{
    return m_defaultLayout;
}

LayoutEngine::LayoutKind TilingController::resolveLayoutKind(LogicalOutput *output) const
{
    KSharedConfigPtr config = KSharedConfig::openConfig(KWIN_CONFIG);
    KConfigGroup tilingGroup(config, QStringLiteral("Tiling"));
    LayoutEngine::LayoutKind kind = globalDefaultLayoutKind();
    if (output) {
        const KConfigGroup outputGroup(&tilingGroup, QStringLiteral("Output %1").arg(output->name()));
        if (outputGroup.hasKey("DefaultLayout")) {
            kind = LayoutEngine::layoutKindFromString(outputGroup.readEntry("DefaultLayout", QString()));
        }
    }
    // If the configured default is not currently enabled, fall back to the
    // first enabled layout so the monitor is always in a usable state.
    if (!isLayoutEnabled(kind)) {
        const QList<LayoutEngine::LayoutKind> enabled = enabledLayoutKinds();
        if (!enabled.isEmpty()) {
            kind = enabled.first();
        }
    }
    return kind;
}

QList<LayoutEngine::LayoutKind> TilingController::enabledLayoutKinds() const
{
    QList<LayoutEngine::LayoutKind> result;
    for (const QString &name : m_enabledLayouts) {
        // Only include kinds the controller actually knows how to build.
        if (name.compare(QLatin1String("MasterStack"), Qt::CaseInsensitive) == 0) {
            result.append(LayoutEngine::LayoutKind::MasterStack);
        } else if (name.compare(QLatin1String("Stacked"), Qt::CaseInsensitive) == 0) {
            result.append(LayoutEngine::LayoutKind::Stacked);
        }
    }
    if (result.isEmpty()) {
        // The user has disabled everything; fall back to the global default so
        // we always have at least one layout available.
        result.append(globalDefaultLayoutKind());
    }
    return result;
}

bool TilingController::isLayoutEnabled(LayoutEngine::LayoutKind kind) const
{
    return enabledLayoutKinds().contains(kind);
}

void TilingController::updateBorders()
{
    if (!m_workspace) {
        return;
    }

    Window *activeWindow = m_workspace->activeWindow();
    const bool activeOnly = (m_borderMode == BorderMode::ActiveOnly);

    for (Window *window : m_workspace->windows()) {
        if (!window || window->isDeleted()) {
            continue;
        }

        const bool isTiled = (window->tilingState().mode == TilingState::Mode::Tiled);
        bool shouldShow = false;
        if (m_borderMode != BorderMode::None && isTiled) {
            if (!activeOnly || window == activeWindow) {
                shouldShow = true;
            }
        }

        const QColor sourceColor = (window == activeWindow) ? m_borderColorActive : m_borderColorInactive;
        const ColorSource source = (window == activeWindow) ? m_colorSourceActive : m_colorSourceInactive;
        const QColor borderColor = resolveColor(source, sourceColor);

        if (isTiled) {
            applyCornerRadius(window);
        }

        TilingState &state = window->tilingState();
        if (state.showBorder != shouldShow || state.borderThickness != m_borderThickness || state.borderColor != borderColor) {
            state.showBorder = shouldShow;
            state.borderThickness = m_borderThickness;
            state.borderColor = borderColor;
            Q_EMIT window->tilingBorderChanged();
        }
    }
}

QColor TilingController::resolveColor(ColorSource source, const QColor &custom) const
{
    switch (source) {
    case ColorSource::SystemAccent:
    case ColorSource::SystemAccentFaded: {
        QColor accent = m_cachedSystemAccent;
        if (source == ColorSource::SystemAccentFaded) {
            accent.setAlpha(128);
        }
        return accent;
    }
    case ColorSource::NoctaliaPrimary:
        return m_noctaliaPrimaryColor.isValid() ? m_noctaliaPrimaryColor : custom;
    case ColorSource::NoctaliaAccent:
        return m_noctaliaAccentColor.isValid() ? m_noctaliaAccentColor : custom;
    case ColorSource::Custom:
    default:
        return custom;
    }
}

void TilingController::readNoctaliaColors()
{
    if (!m_noctaliaConfig) {
        return;
    }
    // KSharedConfig caches the parsed file in memory; reparseConfiguration()
    // forces a re-read so we see changes the user made in noctalia since the
    // last call. Without this, the second-and-onward watcher callbacks
    // would read stale values and the border would never update live.
    m_noctaliaConfig->reparseConfiguration();

    // Noctalia primary: Colors:Selection BackgroundNormal
    KConfigGroup selectionGroup(m_noctaliaConfig, QStringLiteral("Colors:Selection"));
    m_noctaliaPrimaryColor = selectionGroup.readEntry("BackgroundNormal", QColor());

    // Noctalia accent: Colors:Button ForegroundPositive
    KConfigGroup buttonGroup(m_noctaliaConfig, QStringLiteral("Colors:Button"));
    m_noctaliaAccentColor = buttonGroup.readEntry("ForegroundPositive", QColor());
}

void TilingController::readSystemAccent()
{
    if (!m_kdeglobalsConfig) {
        m_kdeglobalsConfig = KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::FullConfig);
    }
    // Same staleness pitfall as readNoctaliaColors().
    m_kdeglobalsConfig->reparseConfiguration();
    // KColorScheme::createApplicationPalette reads [Colors:Selection] and
    // friends from the given config, then returns a QPalette that
    // QPalette::Highlight tracks correctly. Using QGuiApplication::palette()
    // directly would return KWin's default blue, not the user's scheme.
    const QPalette palette = KColorScheme::createApplicationPalette(m_kdeglobalsConfig);
    m_cachedSystemAccent = palette.color(QPalette::Active, QPalette::Highlight);
}

bool TilingController::usesNoctaliaSource() const
{
    return m_colorSourceActive == ColorSource::NoctaliaPrimary
        || m_colorSourceActive == ColorSource::NoctaliaAccent
        || m_colorSourceInactive == ColorSource::NoctaliaPrimary
        || m_colorSourceInactive == ColorSource::NoctaliaAccent;
}

void TilingController::applyCornerRadius(Window *window)
{
    const BorderRadius desired(m_cornerRadius);
    if (window->borderRadius() != desired) {
        window->setBorderRadius(desired);
    }
}

void TilingController::applyGapSettingsToOutput(LogicalOutput *output)
{
    if (!m_workspace || !output) {
        return;
    }

    TileManager *manager = m_workspace->tileManager(output);
    if (!manager) {
        return;
    }

    KSharedConfigPtr config = KSharedConfig::openConfig(KWIN_CONFIG);
    KConfigGroup tilingGroup(config, QStringLiteral("Tiling"));

    // Defaults from the [Tiling] group.
    const qreal defaultGapBetween = tilingGroup.readEntry("GapBetween", 0.0);
    const int defaultGapLeft = tilingGroup.readEntry("GapLeft", 0);
    const int defaultGapRight = tilingGroup.readEntry("GapRight", 0);
    const int defaultGapTop = tilingGroup.readEntry("GapTop", 0);
    const int defaultGapBottom = tilingGroup.readEntry("GapBottom", 0);

    // Per-output override in the [Tiling][Output "name"] sub-group, if any.
    // Entries fall back to the defaults above when not present in the override.
    const QString outputKey = QStringLiteral("Output %1").arg(output->name());
    KConfigGroup outputGroup(&tilingGroup, outputKey);

    const qreal gapBetween = outputGroup.readEntry("GapBetween", defaultGapBetween);
    const int gapLeft = outputGroup.readEntry("GapLeft", defaultGapLeft);
    const int gapRight = outputGroup.readEntry("GapRight", defaultGapRight);
    const int gapTop = outputGroup.readEntry("GapTop", defaultGapTop);
    const int gapBottom = outputGroup.readEntry("GapBottom", defaultGapBottom);
    const QMarginsF gapMargins(gapLeft, gapTop, gapRight, gapBottom);

    for (VirtualDesktop *desktop : VirtualDesktopManager::self()->desktops()) {
        if (RootTile *root = manager->rootTile(desktop)) {
            root->setGapBetween(gapBetween);
            root->setGapMargins(gapMargins);
        }
    }
}

void TilingController::onWindowAdded(Window *window)
{
    if (!m_enabled || !window) {
        return;
    }

    // Watch for mouse-driven moves so we can swap with the window under the
    // cursor on release rather than treating the dragged window as a new one.
    connect(window, &Window::interactiveMoveResizeStarted,
            this, &TilingController::onInteractiveMoveResizeStarted,
            Qt::UniqueConnection);
    connect(window, &Window::interactiveMoveResizeFinished,
            this, &TilingController::onInteractiveMoveResizeFinished,
            Qt::UniqueConnection);

    // When the window is moved between desktops (e.g. via the
    // "Window to Next/Previous/Up/Down Desktop" shortcuts) the layout engines
    // need to migrate the window so it stays tiled on the new desktop and the
    // old layout reflows to fill the empty slot.
    //
    // NOTE: Qt::UniqueConnection does not work with lambda receivers (Qt
    // silently drops the connection with a runtime warning), so we omit it
    // here. onWindowAdded is invoked exactly once per window via the
    // windowAdded signal, so the connection is naturally single-shot per
    // window and there is no need to deduplicate.
    connect(window, &Window::desktopsChanged, this,
            [this, window]() { onWindowDesktopsChanged(window); });

    // When the decoration is (re-)applied, it overwrites the window's
    // borderRadius. Re-apply our corner radius if the window is tiled.
    connect(window, &Window::decorationChanged, this, [this, window]() {
        if (window->tilingState().mode == TilingState::Mode::Tiled) {
            applyCornerRadius(window);
        }
    });

    // Don't touch already-managed windows (e.g. on-all-desktops already handled).
    if (window->tilingState().mode != TilingState::Mode::Floating) {
        updateBorders();
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

    updateBorders();
}

void TilingController::onWindowRemoved(Window *window)
{
    if (!window) {
        return;
    }
    removeWindowFromLayouts(window);
    updateBorders();
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

    setupLayoutEngine(manager, desktop, resolveLayoutKind(output));

    LayoutEngine *engine = manager->layoutEngine(desktop);
    if (!engine) {
        qWarning() << "TilingController: failed to obtain engine for output"
                   << output->name() << "desktop" << desktop->id();
        return;
    }

    engine->addWindow(window);

    // If the window did not end up managed, surface the failure in logs but
    // do NOT silently flip the mode to Floating. The caller (migrateWindow or
    // onWindowAdded) is responsible for the mode, and the next desktop/output
    // change will trigger updateWindowVisibilityAndActivateOnDesktopChange to
    // re-evaluate the tile and snap the geometry correctly.
    if (!layoutEngineForWindow(window)) {
        qWarning() << "TilingController: window" << window->caption()
                   << "was not managed by any layout engine after addWindow; "
                      "leaving tilingState().mode untouched";
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

    // Any active resize context for this window is now stale.
    m_activeResizes.remove(window);
}

void TilingController::migrateWindow(Window *window, LogicalOutput *newOutput, VirtualDesktop *newDesktop)
{
    if (!m_workspace || !window || !newOutput || !newDesktop) {
        return;
    }

    // Find whichever engine currently owns this window, if any. The lookup is
    // an O(outputs * desktops) walk; that's acceptable because desktop/output
    // changes are infrequent (single keypresses).
    LogicalOutput *oldOutput = nullptr;
    VirtualDesktop *oldDesktop = nullptr;
    LayoutEngine *oldEngine = layoutEngineForWindow(window, &oldOutput, &oldDesktop);

    // Nothing to do if the window is already in the engine for the destination
    // (output, desktop) pair. Avoids accidental reflows on no-op changes.
    if (oldEngine && oldOutput == newOutput && oldDesktop == newDesktop) {
        return;
    }

    // Release the source: the engine reflows, so the remaining tiled windows
    // on the source desktop fill the freed slot.
    if (oldEngine) {
        oldEngine->removeWindow(window);
    }

    // Join the destination engine. addWindowToLayout creates the engine on
    // the new (output, desktop) if one doesn't exist yet, so the first time
    // a window ever lands on a desktop it still tiles correctly.
    addWindowToLayout(window, newOutput, newDesktop);
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
        // Float at a default size centered under the cursor, respecting min/max.
        constexpr qreal defaultWidth = 800.0;
        constexpr qreal defaultHeight = 600.0;
        const QSizeF min = window->minSize();
        const QSizeF max = window->maxSize();
        qreal w = std::max(defaultWidth, min.width());
        qreal h = std::max(defaultHeight, min.height());
        if (max.width() > 0) {
            w = std::min(w, max.width());
        }
        if (max.height() > 0) {
            h = std::min(h, max.height());
        }
        const QPointF cursorPos = Cursors::self()->mouse()->pos();
        RectF geom(cursorPos.x() - w / 2, cursorPos.y() - h / 2, w, h);
        if (m_workspace) {
            const RectF screenArea = m_workspace->clientArea(PlacementArea, window);
            geom = window->keepInArea(geom, screenArea);
        }
        window->moveResize(geom);
    } else {
        state.mode = TilingState::Mode::Tiled;
        state.userToggledFloat = false;
        LogicalOutput *output = window->output() ? window->output() : m_workspace->activeOutput();
        VirtualDesktop *desktop = window->desktops().isEmpty()
            ? VirtualDesktopManager::self()->currentDesktop(output)
            : window->desktops().constFirst();
        addWindowToLayout(window, output, desktop);
    }

    updateBorders();
}

void TilingController::onInteractiveMoveResizeStarted()
{
    Window *window = qobject_cast<Window *>(sender());
    if (!window) {
        return;
    }
    LayoutEngine *engine = layoutEngineForWindow(window);
    if (!engine) {
        return;
    }

    if (window->isInteractiveResize()) {
        // Resize path: capture the source leaf and the edge being dragged so
        // endInteractiveResize can convert the final pixel delta into a
        // weight delta on the engine.
        const Gravity gravity = window->interactiveMoveResizeGravity();
        Qt::Edge edge = Qt::RightEdge;
        switch (gravity) {
        case Gravity::Left:
        case Gravity::TopLeft:
        case Gravity::BottomLeft:
            edge = Qt::LeftEdge;
            break;
        case Gravity::Right:
        case Gravity::TopRight:
        case Gravity::BottomRight:
            edge = Qt::RightEdge;
            break;
        case Gravity::Top:
            edge = Qt::TopEdge;
            break;
        case Gravity::Bottom:
            edge = Qt::BottomEdge;
            break;
        case Gravity::None:
            // No specific edge; fall back to right (most common grab).
            edge = Qt::RightEdge;
            break;
        }

        ResizeContext context;
        context.engine = engine;
        context.originalGeometry = window->moveResizeGeometry();
        context.edge = edge;
        m_activeResizes[window] = context;
        return;
    }

    if (!window->isInteractiveMove()) {
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
    if (!window) {
        return;
    }

    // Resize path takes precedence over move. If the user both moved and
    // resized in the same gesture, the resize context still applies.
    if (m_activeResizes.contains(window)) {
        const ResizeContext context = m_activeResizes.value(window);
        const RectF finalGeometry = window->moveResizeGeometry();
        endInteractiveResize(window, finalGeometry, context.edge);
        return;
    }

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

void TilingController::onWindowDesktopsChanged(Window *window)
{
    if (!m_enabled || !m_workspace || !window) {
        return;
    }

    // Floating windows are not part of any layout engine; nothing to migrate.
    if (window->tilingState().mode != TilingState::Mode::Tiled) {
        return;
    }

    // On-all-desktops or multi-desktop windows stay managed by whatever
    // layout engine they were already in; the new desktop set is not a
    // request to re-tile.
    if (window->isOnAllDesktops() || window->desktops().size() != 1) {
        return;
    }

    VirtualDesktop *newDesktop = window->desktops().constFirst();
    if (!newDesktop) {
        return;
    }

    LogicalOutput *output = window->output() ? window->output() : m_workspace->activeOutput();
    if (!output) {
        return;
    }

    // Use the shared migration helper so the desktop-change path is
    // identical to the monitor-change path. The helper finds the old
    // engine, releases the window from it (the source reflows), then adds
    // the window to the engine for the destination (output, desktop).
    migrateWindow(window, output, newDesktop);
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

void TilingController::setLayout(LayoutEngine::LayoutKind kind)
{
    if (!m_workspace) {
        return;
    }

    LogicalOutput *output = m_workspace->activeOutput();
    if (!output) {
        return;
    }

    VirtualDesktop *desktop = VirtualDesktopManager::self()->currentDesktop(output);
    if (!desktop) {
        return;
    }

    setLayoutOn(output, desktop, kind);
}

void TilingController::setLayoutOn(LogicalOutput *output, VirtualDesktop *desktop, LayoutEngine::LayoutKind kind)
{
    if (!m_workspace || !output || !desktop) {
        return;
    }

    // If the user disabled this kind in the kcm, fall back to the first
    // enabled layout so setLayout / cycleLayout never silently do nothing.
    if (!isLayoutEnabled(kind)) {
        const QList<LayoutEngine::LayoutKind> enabled = enabledLayoutKinds();
        if (enabled.isEmpty()) {
            return;
        }
        kind = enabled.first();
    }

    TileManager *manager = m_workspace->tileManager(output);
    if (!manager) {
        return;
    }

    LayoutEngine *existing = manager->layoutEngine(desktop);
    if (!existing) {
        // No engine yet — just create one in the requested kind.
        setupLayoutEngine(manager, desktop, kind);
        return;
    }

    if (existing->layoutKind() == kind) {
        // Already the desired layout; nothing to do.
        return;
    }

    // Take ownership of the current windows so we can re-add them in the same
    // order once the new engine is in place.
    const QList<Window *> carriedWindows = existing->windows();

    auto engine = createLayoutEngine(kind, manager);
    manager->setLayoutEngine(desktop, std::move(engine));

    LayoutEngine *fresh = manager->layoutEngine(desktop);
    if (!fresh) {
        return;
    }

    for (Window *w : carriedWindows) {
        if (!w || w->isDeleted()) {
            continue;
        }
        fresh->addWindow(w);
    }
}

void TilingController::reconcileLayoutKinds()
{
    if (!m_enabled || !m_workspace) {
        return;
    }

    for (LogicalOutput *output : m_workspace->outputs()) {
        if (!output) {
            continue;
        }
        const LayoutEngine::LayoutKind kind = resolveLayoutKind(output);
        for (VirtualDesktop *desktop : VirtualDesktopManager::self()->desktops()) {
            setLayoutOn(output, desktop, kind);
        }
    }
}

void TilingController::cycleLayout()
{
    if (!m_workspace) {
        return;
    }
    LogicalOutput *output = m_workspace->activeOutput();
    if (!output) {
        return;
    }
    VirtualDesktop *desktop = VirtualDesktopManager::self()->currentDesktop(output);
    if (!desktop) {
        return;
    }
    TileManager *manager = m_workspace->tileManager(output);
    if (!manager) {
        return;
    }

    const QList<LayoutEngine::LayoutKind> enabled = enabledLayoutKinds();
    if (enabled.size() < 2) {
        // Nothing to cycle through.
        return;
    }

    // Detect the kind of the engine currently attached to this (output, desktop)
    // pair so the cycle picks the *next* one rather than always the first.
    LayoutEngine::LayoutKind currentKind = globalDefaultLayoutKind();
    if (LayoutEngine *current = manager->layoutEngine(desktop)) {
        currentKind = current->layoutKind();
    }

    int currentIndex = enabled.indexOf(currentKind);
    if (currentIndex < 0) {
        currentIndex = 0;
    }
    const int nextIndex = (currentIndex + 1) % enabled.size();
    setLayout(enabled.at(nextIndex));
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

    // Capture the desktop before sendToOutput, because the helper's lookup of
    // the old engine needs to find the engine on (oldOutput, oldDesktop).
    // The window is moved to the same desktop it was on, just on the new
    // output. If the window was on all desktops or had no desktop, fall back
    // to the target output's current desktop.
    VirtualDesktop *desktop = window->desktops().isEmpty()
        ? VirtualDesktopManager::self()->currentDesktop(targetOutput)
        : window->desktops().constFirst();
    if (!desktop) {
        return;
    }

    // Move window to target output, preserving desktop membership.
    window->sendToOutput(targetOutput);

    // Use the shared migration helper so monitor moves and desktop moves
    // follow the exact same engine-swap path. The helper is idempotent for
    // no-op moves.
    migrateWindow(window, targetOutput, desktop);
}

void TilingController::growActiveTileSize(Qt::Orientation axis)
{
    if (!m_enabled) {
        return;
    }
    Window *window = activeTiledWindow();
    if (!window) {
        return;
    }
    LayoutEngine *engine = layoutEngineForWindow(window);
    if (!engine || !engine->supportsPerTileResize()) {
        return;
    }
    engine->adjustTileSize(window, m_resizeStep, axis);
}

void TilingController::shrinkActiveTileSize(Qt::Orientation axis)
{
    if (!m_enabled) {
        return;
    }
    Window *window = activeTiledWindow();
    if (!window) {
        return;
    }
    LayoutEngine *engine = layoutEngineForWindow(window);
    if (!engine || !engine->supportsPerTileResize()) {
        return;
    }
    engine->adjustTileSize(window, -m_resizeStep, axis);
}

void TilingController::beginInteractiveResize(Window *window)
{
    if (!window || !m_workspace) {
        return;
    }
    LayoutEngine *engine = layoutEngineForWindow(window);
    if (!engine || !engine->supportsPerTileResize()) {
        return;
    }

    ResizeContext context;
    context.engine = engine;
    context.originalGeometry = window->moveResizeGeometry();
    // Default to right edge; the caller (onInteractiveMoveResizeStarted) is
    // expected to update this with the actual grabbed edge before invoking
    // this helper. We set RightEdge as a safe fallback so an uninitialised
    // context still produces a sensible delta calculation.
    context.edge = Qt::RightEdge;
    m_activeResizes[window] = context;
}

void TilingController::endInteractiveResize(Window *window, const RectF &finalGeometry, Qt::Edge edge)
{
    if (!window || !m_workspace) {
        m_activeResizes.remove(window);
        return;
    }
    auto it = m_activeResizes.find(window);
    if (it == m_activeResizes.end()) {
        return;
    }
    ResizeContext context = it.value();
    m_activeResizes.erase(it);

    if (!context.engine || !context.engine->supportsPerTileResize()) {
        return;
    }

    const RectF before = context.originalGeometry;
    const RectF after = finalGeometry;
    if (before.size() == QSizeF() || after.size() == QSizeF()) {
        return;
    }

    // Translate the geometry delta into a weight/ratio delta on the engine.
    LogicalOutput *output = window->output() ? window->output() : m_workspace->activeOutput();
    if (!output) {
        return;
    }
    const QSizeF outputSize = output->geometry().size();
    if (outputSize.width() <= 0 || outputSize.height() <= 0) {
        return;
    }

    qreal delta = 0.0;
    Qt::Orientation axis = Qt::Vertical;
    switch (edge) {
    case Qt::LeftEdge:
        delta = (before.left() - after.left()) / outputSize.width();
        axis = Qt::Horizontal;
        break;
    case Qt::RightEdge:
        delta = (after.right() - before.right()) / outputSize.width();
        axis = Qt::Horizontal;
        break;
    case Qt::TopEdge:
        delta = (before.top() - after.top()) / outputSize.height();
        axis = Qt::Vertical;
        break;
    case Qt::BottomEdge:
        delta = (after.bottom() - before.bottom()) / outputSize.height();
        axis = Qt::Vertical;
        break;
    }
    if (qFuzzyIsNull(delta)) {
        return;
    }

    // Clamp the delta so a single very-large drag doesn't slam a weight to
    // the rail. 0.5 (half a column / half a screen) is a generous upper
    // bound for any single drag.
    delta = std::clamp(delta, qreal(-0.5), qreal(0.5));

    // The reflow will call leaf->setRelativeGeometry(...) which in turn
    // calls w->moveResize(windowGeometry()) for every window in the leaf.
    // That is the snap-back to the tiled geometry: the user dragged a
    // floating proxy, releasing applies the new weight, and the window
    // lands exactly where the engine wants it.
    context.engine->adjustTileSize(window, delta, axis);
}

} // namespace KWin
