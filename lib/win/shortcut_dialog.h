/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QDialog>
#include <memory>

namespace Ui
{
class ShortcutDialog;
}

namespace KWin::win
{

class KWIN_EXPORT shortcut_dialog : public QDialog
{
    Q_OBJECT
public:
    explicit shortcut_dialog(const QKeySequence& cut);
    ~shortcut_dialog() override;

    void accept() override;
    QKeySequence shortcut() const;

    void allow_shortcut(QKeySequence const& seq);
    void
    reject_shortcut(QKeySequence const& seq, std::string const& action, std::string const& app);

public Q_SLOTS:
    void keySequenceChanged();

Q_SIGNALS:
    void shortcut_changed(QKeySequence const& seq);
    void dialogDone(bool ok);

protected:
    void done(int r) override;

private:
    QKeySequence _shortcut;
    std::unique_ptr<Ui::ShortcutDialog> m_ui;
};

}
