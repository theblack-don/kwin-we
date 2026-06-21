/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tilingkcm.h"

#include <KPluginFactory>
#include <QDBusConnection>
#include <QDBusMessage>
#include <qqml.h>

#include "tilingsettings.h"

K_PLUGIN_FACTORY_WITH_JSON(TilingKCMFactory, "kcm_kwin_tiling.json", registerPlugin<KWin::TilingKCM>();)

namespace KWin
{

TilingKCM::TilingKCM(QObject *parent, const KPluginMetaData &metaData)
    : KQuickManagedConfigModule(parent, metaData)
    , m_settings(new TilingSettings(this))
{
    registerSettings(m_settings);
    qmlRegisterAnonymousType<TilingSettings>("org.kde.kwin.kcm.tiling", 1);
}

TilingKCM::~TilingKCM() = default;

TilingSettings *TilingKCM::settings() const
{
    return m_settings;
}

void TilingKCM::save()
{
    KQuickManagedConfigModule::save();

    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/KWin"),
                                                       QStringLiteral("org.kde.KWin"),
                                                       QStringLiteral("reloadConfig"));
    QDBusConnection::sessionBus().send(message);
}

} // namespace KWin

#include "tilingkcm.moc"
#include "moc_tilingkcm.cpp"
