/*
    KWin - the KDE window manager
    SPDX-FileCopyrightText: 2026 KWin Tiling Fork
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "layoutengine.h"

namespace KWin
{

LayoutEngine::LayoutEngine(QObject *parent)
    : QObject(parent)
{
}

LayoutEngine::~LayoutEngine() = default;

} // namespace KWin
