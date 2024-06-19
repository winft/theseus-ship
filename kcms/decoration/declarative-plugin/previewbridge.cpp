/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only
*/
#include "previewbridge.h"
#include "previewclient.h"
#include "previewitem.h"
#include "previewsettings.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>

#include <KCModule>
#include <KCMultiDialog>
#include <KPluginMetaData>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QVBoxLayout>
#include <QWindow>

namespace KDecoration2
{
namespace Preview
{

static const QString s_pluginName = QStringLiteral("org.kde.kdecoration2");
static const QString s_kcmName = QStringLiteral("org.kde.kdecoration2.kcm");

PreviewBridge::PreviewBridge(QObject* parent)
    : DecorationBridge(parent)
    , m_lastCreatedClient(nullptr)
    , m_lastCreatedSettings(nullptr)
    , m_valid(false)
{
    connect(this, &PreviewBridge::pluginChanged, this, &PreviewBridge::createFactory);
}

PreviewBridge::~PreviewBridge() = default;

std::unique_ptr<DecoratedClientPrivate> PreviewBridge::createClient(DecoratedClient* client,
                                                                    Decoration* decoration)
{
    auto ptr = std::unique_ptr<PreviewClient>(new PreviewClient(client, decoration));
    m_lastCreatedClient = ptr.get();
    return ptr;
}

std::unique_ptr<DecorationSettingsPrivate> PreviewBridge::settings(DecorationSettings* parent)
{
    auto ptr = std::unique_ptr<PreviewSettings>(new PreviewSettings(parent));
    m_lastCreatedSettings = ptr.get();
    return ptr;
}

void PreviewBridge::registerPreviewItem(PreviewItem* item)
{
    m_previewItems.append(item);
}

void PreviewBridge::unregisterPreviewItem(PreviewItem* item)
{
    m_previewItems.removeAll(item);
}

void PreviewBridge::setPlugin(const QString& plugin)
{
    if (m_plugin == plugin) {
        return;
    }
    m_plugin = plugin;
    Q_EMIT pluginChanged();
}

QString PreviewBridge::theme() const
{
    return m_theme;
}

void PreviewBridge::setTheme(const QString& theme)
{
    if (m_theme == theme) {
        return;
    }
    m_theme = theme;
    Q_EMIT themeChanged();
}

QString PreviewBridge::kcmoduleName() const
{
    return m_kcmoduleName;
}

void PreviewBridge::setKcmoduleName(const QString& kcmoduleName)
{
    if (m_kcmoduleName == kcmoduleName) {
        return;
    }
    m_kcmoduleName = kcmoduleName;
    Q_EMIT themeChanged();
}

QString PreviewBridge::plugin() const
{
    return m_plugin;
}

void PreviewBridge::createFactory()
{
    m_factory.clear();

    if (m_plugin.isNull()) {
        setValid(false);
        qWarning() << "Plugin not set";
        return;
    }

    const auto offers = KPluginMetaData::findPlugins(s_pluginName);
    auto item = std::find_if(offers.constBegin(), offers.constEnd(), [this](const auto& plugin) {
        return plugin.pluginId() == m_plugin;
    });
    if (item != offers.constEnd()) {
        m_factory = KPluginFactory::loadFactory(*item).plugin;
    }

    setValid(!m_factory.isNull());
}

bool PreviewBridge::isValid() const
{
    return m_valid;
}

void PreviewBridge::setValid(bool valid)
{
    if (m_valid == valid) {
        return;
    }
    m_valid = valid;
    Q_EMIT validChanged();
}

Decoration* PreviewBridge::createDecoration(QObject* parent)
{
    if (!m_valid) {
        return nullptr;
    }
    QVariantMap args({{QStringLiteral("bridge"), QVariant::fromValue(this)}});
    if (!m_theme.isNull()) {
        args.insert(QStringLiteral("theme"), m_theme);
    }
    return m_factory->create<KDecoration2::Decoration>(parent, QVariantList({args}));
}

DecorationButton* PreviewBridge::createButton(KDecoration2::Decoration* decoration,
                                              KDecoration2::DecorationButtonType type,
                                              QObject* parent)
{
    if (!m_valid) {
        return nullptr;
    }
    return m_factory->create<KDecoration2::DecorationButton>(
        parent, QVariantList({QVariant::fromValue(type), QVariant::fromValue(decoration)}));
}

void PreviewBridge::configure(QQuickItem* ctx)
{
    if (!m_valid) {
        qWarning() << "Cannot show an invalid decoration's configuration dialog";
        return;
    }

    KCMultiDialog* dialog = new KCMultiDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    if (m_lastCreatedClient) {
        dialog->setWindowTitle(m_lastCreatedClient->caption());
    }

    QVariantMap args;
    if (!m_theme.isNull()) {
        args.insert(QStringLiteral("theme"), m_theme);
    }
    Q_ASSERT(!m_kcmoduleName.isEmpty());

    dialog->addModule(KPluginMetaData(s_kcmName + QLatin1Char('/') + m_kcmoduleName), {args});

    connect(dialog, &KCMultiDialog::configCommitted, this, [this] {
        if (m_lastCreatedSettings) {
            Q_EMIT m_lastCreatedSettings->decorationSettings()->reconfigured();
        }
        // Send signal to all kwin instances
        QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/KWin"),
                                                          QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("reloadConfig"));
        QDBusConnection::sessionBus().send(message);
    });

    if (ctx->window()) {
        dialog->winId(); // so it creates windowHandle
        dialog->windowHandle()->setTransientParent(
            QQuickRenderControl::renderWindowFor(ctx->window()));
        dialog->setModal(true);
    }

    dialog->show();
}

BridgeItem::BridgeItem(QObject* parent)
    : QObject(parent)
    , m_bridge(new PreviewBridge())
{
    connect(m_bridge, &PreviewBridge::themeChanged, this, &BridgeItem::themeChanged);
    connect(m_bridge, &PreviewBridge::pluginChanged, this, &BridgeItem::pluginChanged);
    connect(m_bridge, &PreviewBridge::validChanged, this, &BridgeItem::validChanged);
    connect(m_bridge, &PreviewBridge::kcmoduleNameChanged, this, &BridgeItem::kcmoduleNameChanged);
}

BridgeItem::~BridgeItem()
{
    m_bridge->deleteLater();
}

}
}
