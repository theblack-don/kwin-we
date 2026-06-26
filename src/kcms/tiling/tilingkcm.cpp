/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tilingkcm.h"

#include "tilingsettings.h"

#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QScreen>
#include <qqml.h>

K_PLUGIN_FACTORY_WITH_JSON(TilingKCMFactory, "kcm_kwin_tiling.json", registerPlugin<KWin::TilingKCM>();)

namespace KWin
{

namespace
{
constexpr int kDefaultGapLeft = 0;
constexpr int kDefaultGapRight = 0;
constexpr int kDefaultGapTop = 0;
constexpr int kDefaultGapBottom = 0;
constexpr int kDefaultGapBetween = 0;
} // namespace

OutputGapOverride::OutputGapOverride(QString name, QString description, int gapLeft, int gapRight,
                                     int gapTop, int gapBottom, int gapBetween, QString defaultLayout,
                                     QObject *parent)
    : QObject(parent)
    , m_name(std::move(name))
    , m_description(std::move(description))
    , m_gapLeft(gapLeft)
    , m_gapRight(gapRight)
    , m_gapTop(gapTop)
    , m_gapBottom(gapBottom)
    , m_gapBetween(gapBetween)
    , m_defaultLayout(std::move(defaultLayout))
{
}

void OutputGapOverride::setGapLeft(int value)
{
    if (m_gapLeft == value) {
        return;
    }
    m_gapLeft = value;
    Q_EMIT gapLeftChanged();
    Q_EMIT modified();
}

void OutputGapOverride::setGapRight(int value)
{
    if (m_gapRight == value) {
        return;
    }
    m_gapRight = value;
    Q_EMIT gapRightChanged();
    Q_EMIT modified();
}

void OutputGapOverride::setGapTop(int value)
{
    if (m_gapTop == value) {
        return;
    }
    m_gapTop = value;
    Q_EMIT gapTopChanged();
    Q_EMIT modified();
}

void OutputGapOverride::setGapBottom(int value)
{
    if (m_gapBottom == value) {
        return;
    }
    m_gapBottom = value;
    Q_EMIT gapBottomChanged();
    Q_EMIT modified();
}

void OutputGapOverride::setGapBetween(int value)
{
    if (m_gapBetween == value) {
        return;
    }
    m_gapBetween = value;
    Q_EMIT gapBetweenChanged();
    Q_EMIT modified();
}

void OutputGapOverride::setDefaultLayout(const QString &value)
{
    // Always emit modified so the KCM apply button reliably lights up
    // when the user touches the control, even if the value happens to
    // resolve to the same string (e.g. they re-selected it).
    m_defaultLayout = value;
    Q_EMIT defaultLayoutChanged();
    Q_EMIT modified();
}

OutputGapOverridesModel::OutputGapOverridesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    if (qApp) {
        connect(qApp, &QGuiApplication::screenAdded, this, &OutputGapOverridesModel::syncFromScreens);
        connect(qApp, &QGuiApplication::screenRemoved, this, &OutputGapOverridesModel::syncFromScreens);
    }
}

OutputGapOverridesModel::~OutputGapOverridesModel()
{
    qDeleteAll(m_entries);
}

QHash<int, QByteArray> OutputGapOverridesModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {GapLeftRole, "gapLeft"},
        {GapRightRole, "gapRight"},
        {GapTopRole, "gapTop"},
        {GapBottomRole, "gapBottom"},
        {GapBetweenRole, "gapBetween"},
        {DefaultLayoutRole, "defaultLayout"},
        {EntryRole, "entry"},
    };
}

QVariant OutputGapOverridesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    OutputGapOverride *entry = m_entries.at(index.row());
    switch (role) {
    case NameRole:
        return entry->name();
    case DescriptionRole:
        return entry->description();
    case GapLeftRole:
        return entry->gapLeft();
    case GapRightRole:
        return entry->gapRight();
    case GapTopRole:
        return entry->gapTop();
    case GapBottomRole:
        return entry->gapBottom();
    case GapBetweenRole:
        return entry->gapBetween();
    case DefaultLayoutRole:
        return entry->defaultLayout();
    case EntryRole:
        return QVariant::fromValue(entry);
    }
    return {};
}

int OutputGapOverridesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.size();
}

OutputGapOverride *OutputGapOverridesModel::entryForName(const QString &name) const
{
    for (OutputGapOverride *entry : m_entries) {
        if (entry->name() == name) {
            return entry;
        }
    }
    return nullptr;
}

void OutputGapOverridesModel::addEntry(const QString &name, const QString &description, int left, int right,
                                       int top, int bottom, int between, const QString &defaultLayout)
{
    auto *entry = new OutputGapOverride(name, description, left, right, top, bottom, between, defaultLayout, this);
    connect(entry, &OutputGapOverride::modified, this, [this, entry]() {
        if (!m_modified) {
            setModified(true);
        }
    });
    m_entries.append(entry);
}

void OutputGapOverridesModel::setModified(bool modified)
{
    if (m_modified == modified) {
        return;
    }
    m_modified = modified;
    Q_EMIT modifiedChanged();
}

void OutputGapOverridesModel::syncFromScreens()
{
    if (!qApp) {
        return;
    }

    QStringList currentNames;
    for (const QScreen *screen : qApp->screens()) {
        if (!screen) {
            continue;
        }
        currentNames << screen->name();
    }

    if (currentNames == m_screenNames) {
        return;
    }

    // Pull the *current* global defaults once. These become the per-monitor
    // entries' starting values so a newly-connected monitor picks up whatever
    // the user has set globally instead of a hardcoded fallback.
    const TilingSettings *settings = nullptr;
    if (TilingKCM *kcm = qobject_cast<TilingKCM *>(parent())) {
        settings = kcm->settings();
    }
    const int defaultLeft = settings ? settings->gapLeft() : kDefaultGapLeft;
    const int defaultRight = settings ? settings->gapRight() : kDefaultGapRight;
    const int defaultTop = settings ? settings->gapTop() : kDefaultGapTop;
    const int defaultBottom = settings ? settings->gapBottom() : kDefaultGapBottom;
    const int defaultBetween = settings ? settings->gapBetween() : kDefaultGapBetween;
    const QString defaultLayout = settings ? settings->defaultLayout() : QStringLiteral("MasterStack");

    // Drop entries for screens that are no longer connected, but keep their
    // values so the next connect re-applies them.
    QHash<QString, OutputGapOverride *> removed;
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        OutputGapOverride *entry = m_entries.at(i);
        if (!currentNames.contains(entry->name())) {
            beginRemoveRows({}, i, i);
            m_entries.removeAt(i);
            endRemoveRows();
            removed.insert(entry->name(), entry);
        }
    }

    // Add entries for newly connected screens, restoring previously-known values if any.
    for (const QScreen *screen : qApp->screens()) {
        if (!screen) {
            continue;
        }
        const QString name = screen->name();
        if (entryForName(name)) {
            continue;
        }
        QString description = screen->model();
        if (description.isEmpty()) {
            description = QStringLiteral("%1 %2").arg(screen->manufacturer(), name);
        }
        if (description.trimmed().isEmpty()) {
            description = name;
        }

        // Start from the current global defaults so the per-monitor spinboxes
        // show values that match the global default. Only if we have a stored
        // override for this monitor do we restore the previous values.
        int left = defaultLeft;
        int right = defaultRight;
        int top = defaultTop;
        int bottom = defaultBottom;
        int between = defaultBetween;
        QString entryLayout = defaultLayout;
        if (OutputGapOverride *previous = removed.value(name)) {
            left = previous->gapLeft();
            right = previous->gapRight();
            top = previous->gapTop();
            bottom = previous->gapBottom();
            between = previous->gapBetween();
            entryLayout = previous->defaultLayout();
        }

        // If the KCM has already loaded a per-output override from kwinrc
        // (e.g. for a monitor that was disconnected and is now back), honor
        // those stored values over the current global default.
        if (settings) {
            KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
            KConfigGroup tilingGroup(config, QStringLiteral("Tiling"));
            KConfigGroup outputGroup(&tilingGroup, QStringLiteral("Output %1").arg(name));
            if (outputGroup.hasKey("GapLeft")) {
                left = outputGroup.readEntry("GapLeft", left);
            }
            if (outputGroup.hasKey("GapRight")) {
                right = outputGroup.readEntry("GapRight", right);
            }
            if (outputGroup.hasKey("GapTop")) {
                top = outputGroup.readEntry("GapTop", top);
            }
            if (outputGroup.hasKey("GapBottom")) {
                bottom = outputGroup.readEntry("GapBottom", bottom);
            }
            if (outputGroup.hasKey("GapBetween")) {
                between = outputGroup.readEntry("GapBetween", between);
            }
            if (outputGroup.hasKey("DefaultLayout")) {
                entryLayout = outputGroup.readEntry("DefaultLayout", entryLayout);
            }
        }

        beginInsertRows({}, m_entries.size(), m_entries.size());
        addEntry(name, description, left, right, top, bottom, between, entryLayout);
        endInsertRows();
    }

    qDeleteAll(removed);
    m_screenNames = currentNames;
    Q_EMIT countChanged();
}

/**
 * Re-sync every entry to the current global defaults. Used when the user
 * changes a global default (gap value, layout, available layouts) so the
 * per-monitor page reflects "matches default" for any entry that hasn't
 * been explicitly overridden.
 */
void OutputGapOverridesModel::refreshFromDefaults(const TilingSettings *settings)
{
    if (!settings) {
        return;
    }

    const int defaultLeft = settings->gapLeft();
    const int defaultRight = settings->gapRight();
    const int defaultTop = settings->gapTop();
    const int defaultBottom = settings->gapBottom();
    const int defaultBetween = settings->gapBetween();
    const QString defaultLayout = settings->defaultLayout();

    beginResetModel();
    for (OutputGapOverride *entry : std::as_const(m_entries)) {
        // Reset every value to the new global default. setDefaultLayout
        // already emits modified() so the model's modified flag is set.
        entry->setGapLeft(defaultLeft);
        entry->setGapRight(defaultRight);
        entry->setGapTop(defaultTop);
        entry->setGapBottom(defaultBottom);
        entry->setGapBetween(defaultBetween);
        entry->setDefaultLayout(defaultLayout);
    }
    endResetModel();
}

void OutputGapOverridesModel::load(KConfigGroup &tilingGroup, const TilingSettings *settings)
{
    const int defaultLeft = settings ? settings->gapLeft() : kDefaultGapLeft;
    const int defaultRight = settings ? settings->gapRight() : kDefaultGapRight;
    const int defaultTop = settings ? settings->gapTop() : kDefaultGapTop;
    const int defaultBottom = settings ? settings->gapBottom() : kDefaultGapBottom;
    const int defaultBetween = settings ? settings->gapBetween() : kDefaultGapBetween;
    const QString defaultLayout = settings ? settings->defaultLayout() : QStringLiteral("MasterStack");

    beginResetModel();
    qDeleteAll(m_entries);
    m_entries.clear();
    m_screenNames.clear();

    if (qApp) {
        for (const QScreen *screen : qApp->screens()) {
            if (!screen) {
                continue;
            }
            const QString name = screen->name();
            const KConfigGroup outputGroup = tilingGroup.group(QStringLiteral("Output %1").arg(name));

            const int left = outputGroup.readEntry("GapLeft", defaultLeft);
            const int right = outputGroup.readEntry("GapRight", defaultRight);
            const int top = outputGroup.readEntry("GapTop", defaultTop);
            const int bottom = outputGroup.readEntry("GapBottom", defaultBottom);
            const int between = outputGroup.readEntry("GapBetween", defaultBetween);
            const QString entryLayout = outputGroup.readEntry("DefaultLayout", defaultLayout);

            QString description = screen->model();
            if (description.isEmpty()) {
                description = QStringLiteral("%1 %2").arg(screen->manufacturer(), screen->name());
            }
            if (description.trimmed().isEmpty()) {
                description = name;
            }

            addEntry(name, description, left, right, top, bottom, between, entryLayout);
            m_screenNames << name;
        }
    }

    endResetModel();
    Q_EMIT countChanged();
    setModified(false);
}

void OutputGapOverridesModel::save(KConfigGroup &tilingGroup, const TilingSettings *settings)
{
    const int defaultLeft = settings ? settings->gapLeft() : kDefaultGapLeft;
    const int defaultRight = settings ? settings->gapRight() : kDefaultGapRight;
    const int defaultTop = settings ? settings->gapTop() : kDefaultGapTop;
    const int defaultBottom = settings ? settings->gapBottom() : kDefaultGapBottom;
    const int defaultBetween = settings ? settings->gapBetween() : kDefaultGapBetween;
    const QString defaultLayout = settings ? settings->defaultLayout() : QStringLiteral("MasterStack");

    // Remove all existing per-output sub-groups first so disconnected outputs
    // don't keep stale overrides around.
    const QStringList existing = tilingGroup.groupList();
    for (const QString &sub : existing) {
        if (sub.startsWith(QLatin1String("Output "))) {
            tilingGroup.deleteGroup(sub);
        }
    }

    for (OutputGapOverride *entry : std::as_const(m_entries)) {
        const bool gapsAreDefault = entry->gapLeft() == defaultLeft
            && entry->gapRight() == defaultRight
            && entry->gapTop() == defaultTop
            && entry->gapBottom() == defaultBottom
            && entry->gapBetween() == defaultBetween;
        const bool layoutIsDefault = entry->defaultLayout() == defaultLayout;
        if (gapsAreDefault && layoutIsDefault) {
            continue;
        }
        KConfigGroup outputGroup(&tilingGroup, QStringLiteral("Output %1").arg(entry->name()));
        if (!gapsAreDefault) {
            outputGroup.writeEntry("GapLeft", entry->gapLeft());
            outputGroup.writeEntry("GapRight", entry->gapRight());
            outputGroup.writeEntry("GapTop", entry->gapTop());
            outputGroup.writeEntry("GapBottom", entry->gapBottom());
            outputGroup.writeEntry("GapBetween", entry->gapBetween());
        }
        if (!layoutIsDefault) {
            outputGroup.writeEntry("DefaultLayout", entry->defaultLayout());
        }
    }

    setModified(false);
}

void OutputGapOverridesModel::defaults(const TilingSettings *settings)
{
    const int defaultLeft = settings ? settings->gapLeft() : kDefaultGapLeft;
    const int defaultRight = settings ? settings->gapRight() : kDefaultGapRight;
    const int defaultTop = settings ? settings->gapTop() : kDefaultGapTop;
    const int defaultBottom = settings ? settings->gapBottom() : kDefaultGapBottom;
    const int defaultBetween = settings ? settings->gapBetween() : kDefaultGapBetween;
    const QString defaultLayout = settings ? settings->defaultLayout() : QStringLiteral("MasterStack");

    beginResetModel();
    for (OutputGapOverride *entry : std::as_const(m_entries)) {
        entry->setGapLeft(defaultLeft);
        entry->setGapRight(defaultRight);
        entry->setGapTop(defaultTop);
        entry->setGapBottom(defaultBottom);
        entry->setGapBetween(defaultBetween);
        entry->setDefaultLayout(defaultLayout);
    }
    endResetModel();
    setModified(false);
}

bool OutputGapOverridesModel::isDefaults(const TilingSettings *settings) const
{
    const int defaultLeft = settings ? settings->gapLeft() : kDefaultGapLeft;
    const int defaultRight = settings ? settings->gapRight() : kDefaultGapRight;
    const int defaultTop = settings ? settings->gapTop() : kDefaultGapTop;
    const int defaultBottom = settings ? settings->gapBottom() : kDefaultGapBottom;
    const int defaultBetween = settings ? settings->gapBetween() : kDefaultGapBetween;
    const QString defaultLayout = settings ? settings->defaultLayout() : QStringLiteral("MasterStack");

    for (OutputGapOverride *entry : m_entries) {
        if (entry->gapLeft() != defaultLeft
            || entry->gapRight() != defaultRight
            || entry->gapTop() != defaultTop
            || entry->gapBottom() != defaultBottom
            || entry->gapBetween() != defaultBetween
            || entry->defaultLayout() != defaultLayout) {
            return false;
        }
    }
    return true;
}

TilingKCM::TilingKCM(QObject *parent, const KPluginMetaData &metaData)
    : KQuickManagedConfigModule(parent, metaData)
    , m_settings(new TilingSettings(this))
    , m_gapOverridesModel(new OutputGapOverridesModel(this))
{
    registerSettings(m_settings);
    qmlRegisterAnonymousType<TilingSettings>("org.kde.kwin.kcm.tiling", 1);
    qmlRegisterAnonymousType<OutputGapOverride>("org.kde.kwin.kcm.tiling", 1);
    qmlRegisterAnonymousType<OutputGapOverridesModel>("org.kde.kwin.kcm.tiling", 1);

    // The base class's settingsChanged() only re-checks the registered
    // KCoreConfigSkeleton objects; it does not consult the virtual
    // isSaveNeeded() override once a skeleton is present. Lift the
    // per-monitor model's modified state into needsSave ourselves so
    // the Apply button in the KCM shell lights up.
    connect(m_gapOverridesModel, &OutputGapOverridesModel::modifiedChanged,
            this, [this]() {
                const bool modelSaveNeeded = m_gapOverridesModel->isModified();
                const bool settingsSaveNeeded = m_settings->isSaveNeeded();
                setNeedsSave(settingsSaveNeeded || modelSaveNeeded);
                if (modelSaveNeeded) {
                    setRepresentsDefaults(false);
                }
            });

    // When the user changes a *global* default (gap value, default layout,
    // available layouts), refresh the per-monitor entries so they reflect
    // the new defaults. Without this, changing the global default layout
    // does not propagate to monitors that have a stored per-output
    // override (the per-monitor page keeps showing the old value, and the
    // override is written back unchanged on save). With this, every per-
    // monitor entry snaps back to "matches default" the moment the global
    // default changes, and the user only needs to touch the per-monitor
    // page when they want a monitor-specific override.
    connect(m_settings, &TilingSettings::defaultLayoutChanged,
            this, [this]() {
                m_gapOverridesModel->refreshFromDefaults(m_settings);
            });
    connect(m_settings, &TilingSettings::gapLeftChanged,
            this, [this]() {
                m_gapOverridesModel->refreshFromDefaults(m_settings);
            });
    connect(m_settings, &TilingSettings::gapRightChanged,
            this, [this]() {
                m_gapOverridesModel->refreshFromDefaults(m_settings);
            });
    connect(m_settings, &TilingSettings::gapTopChanged,
            this, [this]() {
                m_gapOverridesModel->refreshFromDefaults(m_settings);
            });
    connect(m_settings, &TilingSettings::gapBottomChanged,
            this, [this]() {
                m_gapOverridesModel->refreshFromDefaults(m_settings);
            });
    connect(m_settings, &TilingSettings::gapBetweenChanged,
            this, [this]() {
                m_gapOverridesModel->refreshFromDefaults(m_settings);
            });

    // When the user disables a layout that some monitor's per-output
    // override currently uses, reset those monitors to the global default
    // so setLayout() / cycleLayout() can pick a still-enabled layout for
    // them. Otherwise resolveLayoutKind() would silently fall back to the
    // first enabled layout while the KCM still shows a now-disabled name
    // in the per-monitor combo.
    connect(m_settings, &TilingSettings::enabledLayoutsChanged,
            this, [this]() {
                m_gapOverridesModel->refreshFromDefaults(m_settings);
            });
}

TilingKCM::~TilingKCM() = default;

TilingSettings *TilingKCM::settings() const
{
    return m_settings;
}

OutputGapOverridesModel *TilingKCM::gapOverridesModel() const
{
    return m_gapOverridesModel;
}

void TilingKCM::load()
{
    KQuickManagedConfigModule::load();

    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup tilingGroup(config, QStringLiteral("Tiling"));
    m_gapOverridesModel->load(tilingGroup, m_settings);
}

void TilingKCM::save()
{
    KQuickManagedConfigModule::save();

    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup tilingGroup(config, QStringLiteral("Tiling"));
    m_gapOverridesModel->save(tilingGroup, m_settings);
    tilingGroup.sync();

    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/KWin"),
                                                       QStringLiteral("org.kde.KWin"),
                                                       QStringLiteral("reloadConfig"));
    QDBusConnection::sessionBus().send(message);
}

void TilingKCM::defaults()
{
    KQuickManagedConfigModule::defaults();
    m_gapOverridesModel->defaults(m_settings);
}

bool TilingKCM::isSaveNeeded() const
{
    if (m_settings->isSaveNeeded()) {
        return true;
    }
    return m_gapOverridesModel->isModified();
}

bool TilingKCM::isDefaults() const
{
    if (!m_settings->isDefaults()) {
        return false;
    }
    return m_gapOverridesModel->isDefaults(m_settings);
}

} // namespace KWin

#include "tilingkcm.moc"
#include "moc_tilingkcm.cpp"
