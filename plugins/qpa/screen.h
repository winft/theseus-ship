/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_QPA_SCREEN_H
#define KWIN_QPA_SCREEN_H

#include "base/output.h"

#include <QScopedPointer>
#include <functional>
#include <qpa/qplatformscreen.h>

namespace KWin
{
namespace QPA
{

class Integration;
class PlatformCursor;

class Screen : public QPlatformScreen
{
public:
    explicit Screen(base::output* output, Integration* integration);
    ~Screen() override;

    QString name() const override;
    QRect geometry() const override;
    int depth() const override;
    QImage::Format format() const override;
    QSizeF physicalSize() const override;
    QPlatformCursor* cursor() const override;
    QDpi logicalDpi() const override;
    qreal devicePixelRatio() const override;
    QList<QPlatformScreen*> virtualSiblings() const override;

private:
    base::output* output;
    QScopedPointer<PlatformCursor> m_cursor;
    Integration* m_integration;
};

}
}

#endif
