/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;

private:
    QClipboard::Mode m_mode;
};

Window::Window(QClipboard::Mode mode)
    : QRasterWindow()
    , m_mode(mode)
{
}

Window::~Window() = default;

void Window::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.fillRect(0, 0, width(), height(), Qt::red);
}

void Window::focusInEvent(QFocusEvent* event)
{
    QRasterWindow::focusInEvent(event);
    // TODO: make it work without singleshot
    QTimer::singleShot(100, [this] { qApp->clipboard()->setText(QStringLiteral("test"), m_mode); });
}

int main(int argc, char* argv[])
{
    QClipboard::Mode mode = QClipboard::Clipboard;
    if (argv && !strcmp(argv[argc - 1], "Selection")) {
        mode = QClipboard::Selection;
    }

    QGuiApplication app(argc, argv);
    std::unique_ptr<Window> w(new Window(mode));
    w->setGeometry(QRect(0, 0, 100, 200));
    w->show();

    return app.exec();
}

#include "copy.moc"
