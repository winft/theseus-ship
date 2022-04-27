/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "base/output.h"
#include "render/window_thumbnail_item.h"
#include "win/virtual_desktops.h"

namespace KWin
{

/**
 * The desktop_background_item type is a convenience helper that represents the desktop
 * background on the specified screen, virtual desktop, and activity.
 */
class KWIN_EXPORT desktop_background_item : public render::window_thumbnail_item
{
    Q_OBJECT
    Q_PROPERTY(QString outputName READ outputName WRITE setOutputName NOTIFY outputChanged)
    Q_PROPERTY(KWin::base::output* output READ output WRITE setOutput NOTIFY outputChanged)
    Q_PROPERTY(QString activity READ activity WRITE setActivity NOTIFY activityChanged)
    Q_PROPERTY(
        KWin::win::virtual_desktop* desktop READ desktop WRITE setDesktop NOTIFY desktopChanged)

public:
    explicit desktop_background_item(QQuickItem* parent = nullptr);

    void componentComplete() override;

    QString outputName() const;
    void setOutputName(QString const& name);

    base::output* output() const;
    void setOutput(base::output* output);

    win::virtual_desktop* desktop() const;
    void setDesktop(win::virtual_desktop* desktop);

    QString activity() const;
    void setActivity(QString const& activity);

Q_SIGNALS:
    void outputChanged();
    void desktopChanged();
    void activityChanged();

private:
    void updateWindow();

    base::output* m_output = nullptr;
    win::virtual_desktop* m_desktop = nullptr;
    QString m_activity;
};

} // namespace KWin
