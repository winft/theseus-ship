/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include "ui_shortcut_dialog.h"

#include <QDialog>

namespace KWin::win
{

class KWIN_EXPORT shortcut_dialog : public QDialog
{
    Q_OBJECT
public:
    explicit shortcut_dialog(const QKeySequence& cut);
    void accept() override;
    QKeySequence shortcut() const;

    void allow_shortcut(QKeySequence const& seq);
    void reject_shortcut(QKeySequence const& seq, QString const& action, QString const& app);

public Q_SLOTS:
    void keySequenceChanged();

Q_SIGNALS:
    void shortcut_changed(QKeySequence const& seq);
    void dialogDone(bool ok);

protected:
    void done(int r) override;

private:
    Ui::ShortcutDialog m_ui;
    QKeySequence _shortcut;
};

}
