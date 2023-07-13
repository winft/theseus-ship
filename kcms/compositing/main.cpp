/*
    SPDX-FileCopyrightText: 2013 Antonis Tsiapaliokas <kok3rs@gmail.com>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/


#include "ui_compositing.h"
#include <kwin_compositing_interface.h>

#include <QAction>
#include <QApplication>
#include <QLayout>

#include <kcmodule.h>
#include <KPluginFactory>
#include <kservice.h>

#include <algorithm>
#include <functional>

#include "kwincompositing_setting.h"
#include "kwincompositingdata.h"

static bool isRunningPlasma()
{
    return qgetenv("XDG_CURRENT_DESKTOP") == "KDE";
}

class KWinCompositingKCM : public KCModule
{
    Q_OBJECT
public:
    enum CompositingTypeIndex {
        OPENGL_INDEX = 0,
    };

    explicit KWinCompositingKCM(QObject *parent, const KPluginMetaData &data);

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

private Q_SLOTS:
    void onBackendChanged();
    void reenableGl();

private:
    void init();
    void updateUnmanagedItemStatus();
    bool compositingRequired() const;

    Ui_CompositingForm m_form;

    OrgKdeKwinCompositingInterface *m_compositingInterface;
    KWinCompositingSetting *m_settings;
};

static const QVector<qreal> s_animationMultipliers = {8, 4, 2, 1, 0.5, 0.25, 0.125, 0};

bool KWinCompositingKCM::compositingRequired() const
{
    return m_compositingInterface->platformRequiresCompositing();
}

KWinCompositingKCM::KWinCompositingKCM(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
    , m_compositingInterface(new OrgKdeKwinCompositingInterface(QStringLiteral("org.kde.KWin"), QStringLiteral("/Compositor"), QDBusConnection::sessionBus(), this))
    , m_settings(new KWinCompositingSetting(this))
{
    m_form.setupUi(widget());

    // AnimationDurationFactor should be written to the same place as the lnf to avoid conflicts
    m_settings->findItem("AnimationDurationFactor")->setWriteFlags(KConfigBase::Global | KConfigBase::Notify);

    addConfig(m_settings, widget());

    m_form.glCrashedWarning->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    QAction *reenableGlAction = new QAction(i18n("Re-enable OpenGL detection"), this);
    connect(reenableGlAction, &QAction::triggered, this, &KWinCompositingKCM::reenableGl);
    connect(reenableGlAction, &QAction::triggered, m_form.glCrashedWarning, &KMessageWidget::animatedHide);
    m_form.glCrashedWarning->addAction(reenableGlAction);
    m_form.windowThumbnailWarning->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));

    m_form.compositingLabel->setVisible(!compositingRequired());
    m_form.kcfg_Enabled->setVisible(!compositingRequired());
    m_form.kcfg_WindowsBlockCompositing->setVisible(!compositingRequired());

    connect(this, &KWinCompositingKCM::defaultsIndicatorsVisibleChanged, this, &KWinCompositingKCM::updateUnmanagedItemStatus);

    init();
}

void KWinCompositingKCM::reenableGl()
{
    m_settings->setOpenGLIsUnsafe(false);
    m_settings->save();
}

void KWinCompositingKCM::init()
{
    auto currentIndexChangedSignal = static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged);

    // animation speed
    m_form.animationDurationFactor->setMaximum(s_animationMultipliers.size() - 1);
    connect(m_form.animationDurationFactor, &QSlider::valueChanged, this, [this]() {
        updateUnmanagedItemStatus();
        m_settings->setAnimationDurationFactor(s_animationMultipliers[m_form.animationDurationFactor->value()]);
    });

    if (isRunningPlasma()) {
        m_form.animationSpeedLabel->hide();
        m_form.animationSpeedControls->hide();
    }

    // windowThumbnail
    connect(m_form.kcfg_HiddenPreviews, currentIndexChangedSignal, this,
        [this](int index) {
            if (index == 2) {
                m_form.windowThumbnailWarning->animatedShow();
            } else {
                m_form.windowThumbnailWarning->animatedHide();
            }
        }
    );

    // compositing type
    m_form.backend->addItem(i18n("OpenGL"), CompositingTypeIndex::OPENGL_INDEX);

    connect(m_form.backend, currentIndexChangedSignal, this, &KWinCompositingKCM::onBackendChanged);

    if (m_settings->openGLIsUnsafe()) {
        m_form.glCrashedWarning->animatedShow();
    }
}

void KWinCompositingKCM::onBackendChanged()
{
    updateUnmanagedItemStatus();
}

void KWinCompositingKCM::updateUnmanagedItemStatus()
{
    int backend = KWinCompositingSetting::EnumBackend::OpenGL;
    // const int currentType = m_form.backend->currentData().toInt();
    // switch (currentType) {
    // case CompositingTypeIndex::OPENGL_INDEX:
        // default already set
        // break;
    // }
    const auto animationDuration = s_animationMultipliers[m_form.animationDurationFactor->value()];

    const bool inPlasma = isRunningPlasma();

    auto changed = backend != m_settings->backend();
    if (!inPlasma) {
      changed |= (animationDuration != m_settings->animationDurationFactor());
    }
    unmanagedWidgetChangeState(changed);

    auto defaulted = backend == m_settings->defaultBackendValue();
    if (!inPlasma) {
        defaulted &= animationDuration == m_settings->defaultAnimationDurationFactorValue();
    }

    m_form.backend->setProperty("_kde_highlight_neutral", defaultsIndicatorsVisible() && (backend != m_settings->defaultBackendValue()));
    m_form.backend->update();

    unmanagedWidgetDefaultState(defaulted);
}

void KWinCompositingKCM::load()
{
    KCModule::load();

    // unmanaged items
    m_settings->findItem("AnimationDurationFactor")->readConfig(m_settings->config());
    const double multiplier = m_settings->animationDurationFactor();
    auto const it = std::lower_bound(s_animationMultipliers.begin(), s_animationMultipliers.end(), multiplier, std::greater<qreal>());
    const int index = static_cast<int>(std::distance(s_animationMultipliers.begin(), it));
    m_form.animationDurationFactor->setValue(index);
    m_form.animationDurationFactor->setDisabled(m_settings->isAnimationDurationFactorImmutable());

    m_settings->findItem("Backend")->readConfig(m_settings->config());

    // if (m_settings->backend() == KWinCompositingSetting::EnumBackend::OpenGL) {
        m_form.backend->setCurrentIndex(CompositingTypeIndex::OPENGL_INDEX);
    // }
    m_form.backend->setDisabled(m_settings->isBackendImmutable());

    onBackendChanged();
}

void KWinCompositingKCM::defaults()
{
    KCModule::defaults();

    // unmanaged widgets
    m_form.backend->setCurrentIndex(CompositingTypeIndex::OPENGL_INDEX);
    if (!isRunningPlasma()) {
        // corresponds to 1.0 seconds in s_animationMultipliers
        m_form.animationDurationFactor->setValue(3);
    }
}

void KWinCompositingKCM::save()
{
    int backend = KWinCompositingSetting::EnumBackend::OpenGL;
    // const int currentType = m_form.backend->currentData().toInt();
    // switch (currentType) {
    // case CompositingTypeIndex::OPENGL_INDEX:
        // default already set
        // break;
    // }
    m_settings->setBackend(backend);

    if (!isRunningPlasma()) {
        const auto animationDuration = s_animationMultipliers[m_form.animationDurationFactor->value()];
        m_settings->setAnimationDurationFactor(animationDuration);
    }
    m_settings->save();

    KCModule::save();

    // This clears up old entries that are now migrated to kdeglobals
    KConfig("kwinrc", KConfig::NoGlobals).group("KDE").revertToDefault("AnimationDurationFactor");

    // Send signal to all kwin instances
    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/Compositor"),
                                                      QStringLiteral("org.kde.kwin.Compositing"),
                                                      QStringLiteral("reinit"));
    QDBusConnection::sessionBus().send(message);
}

K_PLUGIN_FACTORY_WITH_JSON(KWinCompositingConfigFactory, "kwincompositing.json",
                           registerPlugin<KWinCompositingKCM>();
                           registerPlugin<KWinCompositingData>();)

#include "main.moc"
