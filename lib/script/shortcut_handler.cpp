/*
    SPDX-FileCopyrightText: 2023 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "shortcut_handler.h"

#include "scripting_logging.h"
#include "singleton_interface.h"

#include <QAction>

namespace KWin::scripting
{

shortcut_handler::shortcut_handler(QObject* parent)
    : QObject(parent)
{
}

void shortcut_handler::classBegin()
{
}

void shortcut_handler::componentComplete()
{
    if (m_name.isEmpty()) {
        qCWarning(KWIN_SCRIPTING) << "ShortcutHandler.name is required";
        return;
    }
    if (m_text.isEmpty()) {
        qCWarning(KWIN_SCRIPTING) << "ShortcutHandler.text is required";
        return;
    }

    QAction* action = new QAction(this);
    connect(action, &QAction::triggered, this, &shortcut_handler::activated);
    action->setObjectName(m_name);
    action->setText(m_text);
    singleton_interface::register_shortcut(m_keySequence, action);
}

QString shortcut_handler::name() const
{
    return m_name;
}

void shortcut_handler::setName(const QString& name)
{
    if (m_action) {
        qCWarning(KWIN_SCRIPTING) << "ShortcutHandler.name cannot be changed";
        return;
    }
    if (m_name != name) {
        m_name = name;
        Q_EMIT nameChanged();
    }
}

QString shortcut_handler::text() const
{
    return m_text;
}

void shortcut_handler::setText(const QString& text)
{
    if (m_text != text) {
        m_text = text;
        if (m_action) {
            m_action->setText(text);
        }
        Q_EMIT textChanged();
    }
}

QVariant shortcut_handler::sequence() const
{
    return m_userSequence;
}

void shortcut_handler::setSequence(const QVariant& sequence)
{
    if (m_action) {
        qCWarning(KWIN_SCRIPTING) << "ShortcutHandler.sequence cannot be changed";
        return;
    }
    if (m_userSequence != sequence) {
        m_userSequence = sequence;
        m_keySequence = QKeySequence::fromString(sequence.toString());
        Q_EMIT sequenceChanged();
    }
}

}
