/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KQuickManagedConfigModule>

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVector>

class KConfigGroup;
class QScreen;
class TilingSettings;

namespace KWin
{

/**
 * Per-output gap override entry, exposed to QML.
 *
 * One entry is created for every currently connected output. Editing the
 * gap values writes them into a per-output kwinrc sub-group so the tiling
 * controller can read them back at runtime.
 */
class OutputGapOverride : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString description READ description CONSTANT)
    Q_PROPERTY(int gapLeft READ gapLeft WRITE setGapLeft NOTIFY gapLeftChanged)
    Q_PROPERTY(int gapRight READ gapRight WRITE setGapRight NOTIFY gapRightChanged)
    Q_PROPERTY(int gapTop READ gapTop WRITE setGapTop NOTIFY gapTopChanged)
    Q_PROPERTY(int gapBottom READ gapBottom WRITE setGapBottom NOTIFY gapBottomChanged)
    Q_PROPERTY(int gapBetween READ gapBetween WRITE setGapBetween NOTIFY gapBetweenChanged)
    Q_PROPERTY(QString defaultLayout READ defaultLayout WRITE setDefaultLayout NOTIFY defaultLayoutChanged)

public:
    explicit OutputGapOverride(QString name, QString description, int gapLeft, int gapRight,
                               int gapTop, int gapBottom, int gapBetween, QString defaultLayout,
                               QObject *parent = nullptr);

    QString name() const { return m_name; }
    QString description() const { return m_description; }

    int gapLeft() const { return m_gapLeft; }
    int gapRight() const { return m_gapRight; }
    int gapTop() const { return m_gapTop; }
    int gapBottom() const { return m_gapBottom; }
    int gapBetween() const { return m_gapBetween; }
    QString defaultLayout() const { return m_defaultLayout; }

    void setGapLeft(int value);
    void setGapRight(int value);
    void setGapTop(int value);
    void setGapBottom(int value);
    void setGapBetween(int value);
    void setDefaultLayout(const QString &value);

Q_SIGNALS:
    void gapLeftChanged();
    void gapRightChanged();
    void gapTopChanged();
    void gapBottomChanged();
    void gapBetweenChanged();
    void defaultLayoutChanged();

    /**
     * Emitted whenever any of the gap values change. The owning model
     * uses this to mark itself as modified.
     */
    void modified();

private:
    QString m_name;
    QString m_description;
    int m_gapLeft;
    int m_gapRight;
    int m_gapTop;
    int m_gapBottom;
    int m_gapBetween;
    QString m_defaultLayout;
};

/**
 * List model of per-output gap overrides backed by kwinrc.
 *
 * The model is populated from the currently connected QScreen instances and
 * the per-output sub-groups under [Tiling] in kwinrc. Edits are kept in the
 * in-memory entries until TilingKCM::save() is called, at which point the
 * overrides are written back to kwinrc.
 */
class OutputGapOverridesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        DescriptionRole,
        GapLeftRole,
        GapRightRole,
        GapTopRole,
        GapBottomRole,
        GapBetweenRole,
        DefaultLayoutRole,
        EntryRole,
    };
    Q_ENUM(Roles)

    explicit OutputGapOverridesModel(QObject *parent = nullptr);
    ~OutputGapOverridesModel() override;

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * Re-populate the model from the current QScreen list and the
     * per-output override sub-groups in the supplied Tiling group.
     */
    void load(KConfigGroup &tilingGroup, const TilingSettings *settings);

    /**
     * Write the current override values into the per-output sub-groups
     * of the supplied Tiling group. Sub-groups whose values are all
     * equal to the defaults are removed to keep kwinrc tidy.
     */
    void save(KConfigGroup &tilingGroup, const TilingSettings *settings);

    /**
     * Reset every override entry to match the default gap values from
     * the settings object.
     */
    void defaults(const TilingSettings *settings);

    /**
     * Returns true if any entry differs from its loaded state. Used by the
     * KCM to drive the Apply button.
     */
    bool isModified() const { return m_modified; }

    /**
     * Returns true if every entry matches the default gap values. Used by
     * the KCM to drive the Defaults button.
     */
    bool isDefaults(const TilingSettings *settings) const;

Q_SIGNALS:
    void countChanged();
    void modifiedChanged();

private:
    void addEntry(const QString &name, const QString &description, int left, int right,
                  int top, int bottom, int between, const QString &defaultLayout);
    void syncFromScreens();
    OutputGapOverride *entryForName(const QString &name) const;
    void setModified(bool modified);

    QVector<OutputGapOverride *> m_entries;
    QStringList m_screenNames;
    bool m_modified = false;
};

/**
 * Tiling settings KCM module.
 *
 * Hosts the default settings (via TilingSettings) and a model of
 * per-output gap overrides. Saving the module writes both the default
 * settings and the per-output override sub-groups to kwinrc, then
 * notifies KWin via DBus to reload the configuration.
 */
class TilingKCM : public KQuickManagedConfigModule
{
    Q_OBJECT
    Q_PROPERTY(TilingSettings *settings READ settings CONSTANT)
    Q_PROPERTY(KWin::OutputGapOverridesModel *gapOverridesModel READ gapOverridesModel CONSTANT)

public:
    explicit TilingKCM(QObject *parent, const KPluginMetaData &metaData);
    ~TilingKCM() override;

    TilingSettings *settings() const;
    OutputGapOverridesModel *gapOverridesModel() const;

    bool isSaveNeeded() const override;
    bool isDefaults() const override;

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

private:
    TilingSettings *m_settings;
    OutputGapOverridesModel *m_gapOverridesModel;
};

} // namespace KWin
