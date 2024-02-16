/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
*/
#ifndef KDECOARTIONS_PREVIEW_CLIENT_H
#define KDECOARTIONS_PREVIEW_CLIENT_H

#include <como/win/deco/palette.h>

#include <KDecoration2/Private/DecoratedClientPrivate>
#include <QObject>
#include <QPalette>

class QAbstractItemModel;

namespace KDecoration2
{
namespace Preview
{
class PreviewClient : public QObject, public ApplicationMenuEnabledDecoratedClientPrivate
{
    Q_OBJECT
    Q_PROPERTY(KDecoration2::Decoration* decoration READ decoration CONSTANT)
    Q_PROPERTY(QString caption READ caption WRITE setCaption NOTIFY captionChanged)
    Q_PROPERTY(QIcon icon READ icon WRITE setIcon NOTIFY iconChanged)
    Q_PROPERTY(QString iconName READ iconName WRITE setIconName NOTIFY iconNameChanged)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool closeable READ isCloseable WRITE setCloseable NOTIFY closeableChanged)
    Q_PROPERTY(bool keepAbove READ isKeepAbove WRITE setKeepAbove NOTIFY keepAboveChanged)
    Q_PROPERTY(bool keepBelow READ isKeepBelow WRITE setKeepBelow NOTIFY keepBelowChanged)
    Q_PROPERTY(bool maximizable READ isMaximizeable WRITE setMaximizable NOTIFY maximizableChanged)
    Q_PROPERTY(bool maximized READ isMaximized NOTIFY maximizedChanged)
    Q_PROPERTY(bool maximizedVertically READ isMaximizedVertically WRITE setMaximizedVertically
                   NOTIFY maximizedVerticallyChanged)
    Q_PROPERTY(bool maximizedHorizontally READ isMaximizedHorizontally WRITE
                   setMaximizedHorizontally NOTIFY maximizedHorizontallyChanged)
    Q_PROPERTY(bool minimizable READ isMinimizeable WRITE setMinimizable NOTIFY minimizableChanged)
    Q_PROPERTY(bool modal READ isModal WRITE setModal NOTIFY modalChanged)
    Q_PROPERTY(bool movable READ isMoveable WRITE setMovable NOTIFY movableChanged)
    Q_PROPERTY(bool onAllDesktops READ isOnAllDesktops NOTIFY onAllDesktopsChanged)
    Q_PROPERTY(bool resizable READ isResizeable WRITE setResizable NOTIFY resizableChanged)
    Q_PROPERTY(bool providesContextHelp READ providesContextHelp WRITE setProvidesContextHelp NOTIFY
                   providesContextHelpChanged)
    Q_PROPERTY(int width READ width WRITE setWidth NOTIFY widthChanged)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY heightChanged)
    Q_PROPERTY(bool bordersTopEdge READ bordersTopEdge WRITE setBordersTopEdge NOTIFY
                   bordersTopEdgeChanged)
    Q_PROPERTY(bool bordersLeftEdge READ bordersLeftEdge WRITE setBordersLeftEdge NOTIFY
                   bordersLeftEdgeChanged)
    Q_PROPERTY(bool bordersRightEdge READ bordersRightEdge WRITE setBordersRightEdge NOTIFY
                   bordersRightEdgeChanged)
    Q_PROPERTY(bool bordersBottomEdge READ bordersBottomEdge WRITE setBordersBottomEdge NOTIFY
                   bordersBottomEdgeChanged)
public:
    explicit PreviewClient(DecoratedClient* client, Decoration* decoration);
    ~PreviewClient() override;

    QString caption() const override;
    WId decorationId() const override;
    WId windowId() const override;
    QString windowClass() const override;
    QIcon icon() const override;
    bool isActive() const override;
    bool isCloseable() const override;
    bool isKeepAbove() const override;
    bool isKeepBelow() const override;
    bool isMaximizeable() const override;
    bool isMaximized() const override;
    bool isMaximizedVertically() const override;
    bool isMaximizedHorizontally() const override;
    bool isMinimizeable() const override;
    bool isModal() const override;
    bool isMoveable() const override;
    bool isOnAllDesktops() const override;
    bool isResizeable() const override;

    // Deprecated
    bool isShadeable() const override
    {
        return false;
    }
    bool isShaded() const override
    {
        return false;
    }

    bool providesContextHelp() const override;

    int width() const override;
    int height() const override;
    QSize size() const override;
    QPalette palette() const override;
    QColor color(ColorGroup group, ColorRole role) const override;
    Qt::Edges adjacentScreenEdges() const override;

    bool hasApplicationMenu() const override;
    bool isApplicationMenuActive() const override;

    void requestShowToolTip(const QString& text) override;
    void requestHideToolTip() override;
    void requestClose() override;
    void requestContextHelp() override;
    void requestToggleMaximization(Qt::MouseButtons buttons) override;
    void requestMinimize() override;
    void requestToggleKeepAbove() override;
    void requestToggleKeepBelow() override;

    // Deprecated
    void requestToggleShade() override
    {
    }

    void requestShowWindowMenu(QRect const& rect) override;
    void requestShowApplicationMenu(const QRect& rect, int actionId) override;
    void requestToggleOnAllDesktops() override;

    void showApplicationMenu(int actionId) override;

    void setCaption(const QString& caption);
    void setActive(bool active);
    void setCloseable(bool closeable);
    void setMaximizable(bool maximizable);
    void setKeepBelow(bool keepBelow);
    void setKeepAbove(bool keepAbove);
    void setMaximizedHorizontally(bool maximized);
    void setMaximizedVertically(bool maximized);
    void setMinimizable(bool minimizable);
    void setModal(bool modal);
    void setMovable(bool movable);
    void setResizable(bool resizable);
    void setProvidesContextHelp(bool contextHelp);

    void setWidth(int width);
    void setHeight(int height);

    QString iconName() const;
    void setIconName(const QString& icon);
    void setIcon(const QIcon& icon);

    bool bordersTopEdge() const;
    bool bordersLeftEdge() const;
    bool bordersRightEdge() const;
    bool bordersBottomEdge() const;

    void setBordersTopEdge(bool enabled);
    void setBordersLeftEdge(bool enabled);
    void setBordersRightEdge(bool enabled);
    void setBordersBottomEdge(bool enabled);

Q_SIGNALS:
    void captionChanged(const QString&);
    void iconChanged(const QIcon&);
    void iconNameChanged(const QString&);
    void activeChanged(bool);
    void closeableChanged(bool);
    void keepAboveChanged(bool);
    void keepBelowChanged(bool);
    void maximizableChanged(bool);
    void maximizedChanged(bool);
    void maximizedVerticallyChanged(bool);
    void maximizedHorizontallyChanged(bool);
    void minimizableChanged(bool);
    void modalChanged(bool);
    void movableChanged(bool);
    void onAllDesktopsChanged(bool);
    void resizableChanged(bool);
    void providesContextHelpChanged(bool);
    void widthChanged(int);
    void heightChanged(int);
    void paletteChanged(const QPalette&);
    void bordersTopEdgeChanged(bool);
    void bordersLeftEdgeChanged(bool);
    void bordersRightEdgeChanged(bool);
    void bordersBottomEdgeChanged(bool);

    void showWindowMenuRequested();
    void showApplicationMenuRequested();
    void minimizeRequested();
    void closeRequested();

private:
    QString m_caption;
    QIcon m_icon;
    QString m_iconName;
    como::win::deco::palette m_palette;
    bool m_active;
    bool m_closeable;
    bool m_keepBelow;
    bool m_keepAbove;
    bool m_maximizable;
    bool m_maximizedHorizontally;
    bool m_maximizedVertically;
    bool m_minimizable;
    bool m_modal;
    bool m_movable;
    bool m_resizable;
    bool m_providesContextHelp;
    bool m_onAllDesktops;
    int m_width;
    int m_height;
    bool m_bordersTopEdge;
    bool m_bordersLeftEdge;
    bool m_bordersRightEdge;
    bool m_bordersBottomEdge;
};

} // namespace Preview
} // namespace KDecoration2

#endif
