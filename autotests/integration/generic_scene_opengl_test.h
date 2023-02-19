/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "lib/app.h"

#include <QObject>

namespace KWin
{

class KWIN_EXPORT GenericSceneOpenGLTest : public QObject
{
    Q_OBJECT
public:
    ~GenericSceneOpenGLTest() override;

protected:
    explicit GenericSceneOpenGLTest(const QByteArray& envVariable);
private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void testRestart();

private:
    QByteArray m_envVariable;
};

}
