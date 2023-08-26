/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "model_helpers.h"
#include "ui_debug_console.h"

#include "kwin_export.h"

#include <render/gl/interface/platform.h>
#include <render/gl/interface/utils.h>

#include <QAbstractItemModel>
#include <QStyledItemDelegate>
#include <memory>
#include <vector>

namespace KWin
{

namespace win
{
class property_window;
}

namespace debug
{

class KWIN_EXPORT console_model : public QAbstractItemModel
{
    Q_OBJECT
public:
    ~console_model() override;

    template<typename Space>
    static console_model* create(Space& space, QObject* parent = nullptr)
    {
        auto model = new console_model(parent);
        model_setup_connections(*model, space);
        return model;
    }

    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    int rowCount(const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;

    // Wrap QAbstractItemModel functions for public consumption from free functions.
    QModelIndex create_index(int row, int column, quintptr id) const;
    void begin_insert_rows(QModelIndex const& parent, int first, int last);
    void end_insert_rows();
    void begin_remove_rows(QModelIndex const& parent, int first, int last);
    void end_remove_rows();

    virtual bool get_client_count(int parent_id, int& count) const;
    virtual bool get_property_count(QModelIndex const& parent, int& count) const;

    virtual bool get_client_index(int row, int column, int parent_id, QModelIndex& index) const;
    virtual bool
    get_property_index(int row, int column, QModelIndex const& parent, QModelIndex& index) const;

    virtual QVariant get_client_data(QModelIndex const& index, int role) const;
    virtual QVariant get_client_property_data(QModelIndex const& index, int role) const;

    QVariant propertyData(QObject* object, const QModelIndex& index, int role) const;

    win::property_window* x11Client(QModelIndex const& index) const;
    win::property_window* unmanaged(QModelIndex const& index) const;
    virtual int topLevelRowCount() const;

    static constexpr int s_x11ClientId{1};
    static constexpr int s_x11UnmanagedId{2};
    static constexpr int s_waylandClientId{3};
    static constexpr int s_workspaceInternalId{4};

    std::vector<std::unique_ptr<win::property_window>> m_x11Clients;
    std::vector<std::unique_ptr<win::property_window>> m_unmanageds;

protected:
    explicit console_model(QObject* parent = nullptr);
};

class KWIN_EXPORT console_delegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit console_delegate(QObject* parent = nullptr);
    ~console_delegate() override;

    QString displayText(const QVariant& value, const QLocale& locale) const override;
};

template<typename Space>
class console : public QWidget
{
public:
    console(Space& space)
        : QWidget()
        , m_ui(new Ui::debug_console)
        , space{space}
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        this->setWindowTitle("kwin_debugconsole");

        m_ui->setupUi(this);

        m_ui->quitButton->setIcon(QIcon::fromTheme(QStringLiteral("application-exit")));
        m_ui->tabWidget->setTabIcon(0, QIcon::fromTheme(QStringLiteral("view-list-tree")));
        m_ui->tabWidget->setTabIcon(1, QIcon::fromTheme(QStringLiteral("view-list-tree")));

        connect(m_ui->quitButton, &QAbstractButton::clicked, this, &console::deleteLater);

        initGLTab(*space.base.render->compositor->scene);
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);

        // delay the connection to the show event as in ctor the windowHandle returns null
        connect(windowHandle(), &QWindow::visibleChanged, this, [this](bool visible) {
            if (visible) {
                // ignore
                return;
            }
            deleteLater();
        });
    }

    template<typename Scene>
    void initGLTab(Scene& scene)
    {
        if (!scene.platform.compositor->effects
            || !scene.platform.compositor->effects->isOpenGLCompositing()) {
            m_ui->noOpenGLLabel->setVisible(true);
            m_ui->glInfoScrollArea->setVisible(false);
            return;
        }
        GLPlatform* gl = GLPlatform::instance();
        m_ui->noOpenGLLabel->setVisible(false);
        m_ui->glInfoScrollArea->setVisible(true);
        m_ui->glVendorStringLabel->setText(QString::fromLocal8Bit(gl->glVendorString()));
        m_ui->glRendererStringLabel->setText(QString::fromLocal8Bit(gl->glRendererString()));
        m_ui->glVersionStringLabel->setText(QString::fromLocal8Bit(gl->glVersionString()));
        m_ui->glslVersionStringLabel->setText(
            QString::fromLocal8Bit(gl->glShadingLanguageVersionString()));
        m_ui->glDriverLabel->setText(GLPlatform::driverToString(gl->driver()));
        m_ui->glGPULabel->setText(GLPlatform::chipClassToString(gl->chipClass()));
        m_ui->glVersionLabel->setText(GLPlatform::versionToString(gl->glVersion()));
        m_ui->glslLabel->setText(GLPlatform::versionToString(gl->glslVersion()));

        auto extensionsString = [](const auto& extensions) {
            QString text = QStringLiteral("<ul>");
            for (auto extension : extensions) {
                text.append(QStringLiteral("<li>%1</li>").arg(QString::fromLocal8Bit(extension)));
            }
            text.append(QStringLiteral("</ul>"));
            return text;
        };

        m_ui->platformExtensionsLabel->setText(
            extensionsString(scene.openGLPlatformInterfaceExtensions()));
        m_ui->openGLExtensionsLabel->setText(extensionsString(openGLExtensions()));
    }

    QScopedPointer<Ui::debug_console> m_ui;
    Space& space;
};

}
}
