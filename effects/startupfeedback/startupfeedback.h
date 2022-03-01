/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

 Copyright (C) 2010 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#ifndef KWIN_STARTUPFEEDBACK_H
#define KWIN_STARTUPFEEDBACK_H
#include <KConfigWatcher>
#include <KStartupInfo>
#include <QObject>
#include <kwineffects.h>

#include <chrono>

class KSelectionOwner;
namespace KWin
{
class GLTexture;

class StartupFeedbackEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(int type READ type)
public:
    StartupFeedbackEffect();
    ~StartupFeedbackEffect() override;

    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(ScreenPrePaintData& data, std::chrono::milliseconds presentTime) override;
    void paintScreen(int mask, const QRegion& region, ScreenPaintData& data) override;
    void postPaintScreen() override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 90;
    }

    int type() const
    {
        return int(m_type);
    }

    static bool supported();

private Q_SLOTS:
    void gotNewStartup(const QString& id, const QIcon& icon);
    void gotRemoveStartup(const QString& id);
    void gotStartupChange(const QString& id, const QIcon& icon);
    void slotMouseChanged(const QPoint& pos,
                          const QPoint& oldpos,
                          Qt::MouseButtons buttons,
                          Qt::MouseButtons oldbuttons,
                          Qt::KeyboardModifiers modifiers,
                          Qt::KeyboardModifiers oldmodifiers);

private:
    enum FeedbackType { NoFeedback, BouncingFeedback, BlinkingFeedback, PassiveFeedback };

    struct Startup {
        QIcon icon;
        QSharedPointer<QTimer> expiredTimer;
    };

    void start(const Startup& startup);
    void stop();
    QImage scalePixmap(const QPixmap& pm, const QSize& size) const;
    void prepareTextures(const QPixmap& pix);
    QRect feedbackRect() const;

    qreal m_bounceSizesRatio;
    KStartupInfo* m_startupInfo;
    KSelectionOwner* m_selection;
    QString m_currentStartup;
    QMap<QString, Startup> m_startups;
    bool m_active;
    int m_frame;
    int m_progress;
    std::chrono::milliseconds m_lastPresentTime;
    QScopedPointer<GLTexture> m_bouncingTextures[5];
    QScopedPointer<GLTexture> m_texture; // for passive and blinking
    FeedbackType m_type;
    QRect m_currentGeometry, m_dirtyRect;
    QScopedPointer<GLShader> m_blinkingShader;
    int m_cursorSize;
    KConfigWatcher::Ptr m_configWatcher;
    bool m_splashVisible;
    std::chrono::seconds m_timeout;
};
} // namespace

#endif
