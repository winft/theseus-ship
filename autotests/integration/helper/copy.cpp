/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QRasterWindow>
#include <QTimer>

class Window : public QRasterWindow
{
    Q_OBJECT
public:
    explicit Window(QClipboard::Mode mode);
    ~Window() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;

private:
    QClipboard::Mode m_mode;
};

Window::Window(QClipboard::Mode mode)
    : QRasterWindow()
    , m_mode(mode)
{
}

Window::~Window() = default;

void Window::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.fillRect(0, 0, width(), height(), Qt::red);
}

void Window::focusInEvent(QFocusEvent *event)
{
    QRasterWindow::focusInEvent(event);
    // TODO: make it work without singleshot
    QTimer::singleShot(100,[this] {
        qApp->clipboard()->setText(QStringLiteral("test"), m_mode);
    });
}

int main(int argc, char *argv[])
{
    QClipboard::Mode mode = QClipboard::Clipboard;
    if (argv && !strcmp(argv[argc-1], "Selection")) {
        mode = QClipboard::Selection;
    }

    QGuiApplication app(argc, argv);
    std::unique_ptr<Window> w(new Window(mode));
    w->setGeometry(QRect(0, 0, 100, 200));
    w->show();

    return app.exec();
}

#include "copy.moc"
