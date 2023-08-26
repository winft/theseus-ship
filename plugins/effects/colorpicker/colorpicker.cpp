/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "colorpicker.h"

#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/utils.h>
#include <render/gl/interface/utils_funcs.h>

#include <KLocalizedString>
#include <QDBusConnection>
#include <QDBusMetaType>

Q_DECLARE_METATYPE(QColor)

QDBusArgument& operator<<(QDBusArgument& argument, const QColor& color)
{
    argument.beginStructure();
    argument << color.rgba();
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, QColor& color)
{
    argument.beginStructure();
    QRgb rgba;
    argument >> rgba;
    argument.endStructure();
    color = QColor::fromRgba(rgba);
    return argument;
}

namespace KWin
{

bool ColorPickerEffect::supported()
{
    return effects->isOpenGLCompositing();
}

ColorPickerEffect::ColorPickerEffect()
    : m_scheduledPosition(QPoint(-1, -1))
{
    qDBusRegisterMetaType<QColor>();
    QDBusConnection::sessionBus().registerObject(
        QStringLiteral("/ColorPicker"), this, QDBusConnection::ExportScriptableContents);
}

ColorPickerEffect::~ColorPickerEffect() = default;

void ColorPickerEffect::paintScreen(effect::screen_paint_data& data)
{
    effects->paintScreen(data);

    if (m_scheduledPosition != QPoint(-1, -1)
        && (!data.screen || data.screen->geometry().contains(m_scheduledPosition))) {
        uint8_t pix_data[4];
        constexpr GLsizei PIXEL_SIZE = 1;
        auto const texturePosition
            = (data.render.projection * data.render.view).map(m_scheduledPosition);

        glReadnPixels(texturePosition.x(),
                      data.render.viewport.height() - texturePosition.y() - PIXEL_SIZE,
                      PIXEL_SIZE,
                      PIXEL_SIZE,
                      GL_RGBA,
                      GL_UNSIGNED_BYTE,
                      4,
                      pix_data);

        QDBusConnection::sessionBus().send(
            m_replyMessage.createReply(QColor(pix_data[0], pix_data[1], pix_data[2])));
        m_picking = false;
        m_scheduledPosition = QPoint(-1, -1);
    }
}

QColor ColorPickerEffect::pick()
{
    if (!calledFromDBus()) {
        return QColor();
    }
    if (m_picking) {
        sendErrorReply(QDBusError::Failed, "Color picking is already in progress");
        return QColor();
    }
    m_picking = true;
    m_replyMessage = message();
    setDelayedReply(true);
    showInfoMessage();
    effects->startInteractivePositionSelection([this](const QPoint& p) {
        hideInfoMessage();
        if (p == QPoint(-1, -1)) {
            // error condition
            QDBusConnection::sessionBus().send(m_replyMessage.createErrorReply(
                QStringLiteral("org.kde.kwin.ColorPicker.Error.Cancelled"),
                "Color picking got cancelled"));
            m_picking = false;
        } else {
            m_scheduledPosition = p;
            effects->addRepaintFull();
        }
    });
    return QColor();
}

void ColorPickerEffect::showInfoMessage()
{
    effects->showOnScreenMessage(i18n("Select a position for color picking with left click or "
                                      "enter.\nEscape or right click to cancel."),
                                 QStringLiteral("color-picker"));
}

void ColorPickerEffect::hideInfoMessage()
{
    effects->hideOnScreenMessage();
}

bool ColorPickerEffect::isActive() const
{
    return m_picking && ((m_scheduledPosition != QPoint(-1, -1))) && !effects->isScreenLocked();
}

} // namespace
