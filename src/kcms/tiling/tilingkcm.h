/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KQuickManagedConfigModule>

class TilingSettings;

namespace KWin
{

class TilingKCM : public KQuickManagedConfigModule
{
    Q_OBJECT
    Q_PROPERTY(TilingSettings *settings READ settings CONSTANT)

public:
    explicit TilingKCM(QObject *parent, const KPluginMetaData &metaData);
    ~TilingKCM() override;

    TilingSettings *settings() const;

public Q_SLOTS:
    void save() override;

private:
    TilingSettings *m_settings;
};

} // namespace KWin
