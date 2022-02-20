/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/control/keyboard.h"
#include "input/control/pointer.h"
#include "input/control/switch.h"
#include "input/control/touch.h"

#include "kwin_export.h"

#include <QObject>
#include <QSizeF>

namespace KWin::input::dbus
{

class KWIN_EXPORT device : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.InputDevice")

    //
    // General
    Q_PROPERTY(bool keyboard READ isKeyboard CONSTANT)
    Q_PROPERTY(bool alphaNumericKeyboard READ isAlphaNumericKeyboard CONSTANT)
    Q_PROPERTY(bool pointer READ isPointer CONSTANT)
    Q_PROPERTY(bool touchpad READ isTouchpad CONSTANT)
    Q_PROPERTY(bool touch READ isTouch CONSTANT)
    Q_PROPERTY(bool tabletTool READ isTabletTool CONSTANT)
    Q_PROPERTY(bool tabletPad READ isTabletPad CONSTANT)
    Q_PROPERTY(bool gestureSupport READ supportsGesture CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString sysName READ sysName CONSTANT)
    Q_PROPERTY(QString outputName READ outputName CONSTANT)
    Q_PROPERTY(QSizeF size READ size CONSTANT)
    Q_PROPERTY(quint32 product READ product CONSTANT)
    Q_PROPERTY(quint32 vendor READ vendor CONSTANT)
    Q_PROPERTY(bool supportsDisableEvents READ supportsDisableEvents CONSTANT)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

    //
    // Advanced
    Q_PROPERTY(int supportedButtons READ supportedButtons CONSTANT)
    Q_PROPERTY(bool supportsCalibrationMatrix READ supportsCalibrationMatrix CONSTANT)

    Q_PROPERTY(bool supportsLeftHanded READ supportsLeftHanded CONSTANT)
    Q_PROPERTY(bool leftHandedEnabledByDefault READ leftHandedEnabledByDefault CONSTANT)
    Q_PROPERTY(bool leftHanded READ isLeftHanded WRITE setLeftHanded NOTIFY leftHandedChanged)

    Q_PROPERTY(bool supportsDisableEventsOnExternalMouse READ supportsDisableEventsOnExternalMouse
                   CONSTANT)

    Q_PROPERTY(bool supportsDisableWhileTyping READ supportsDisableWhileTyping CONSTANT)
    Q_PROPERTY(
        bool disableWhileTypingEnabledByDefault READ disableWhileTypingEnabledByDefault CONSTANT)
    Q_PROPERTY(bool disableWhileTyping READ isDisableWhileTyping WRITE setDisableWhileTyping NOTIFY
                   disableWhileTypingChanged)

    //
    // Acceleration speed and profile
    Q_PROPERTY(bool supportsPointerAcceleration READ supportsPointerAcceleration CONSTANT)
    Q_PROPERTY(qreal defaultPointerAcceleration READ defaultPointerAcceleration CONSTANT)
    Q_PROPERTY(qreal pointerAcceleration READ pointerAcceleration WRITE setPointerAcceleration
                   NOTIFY pointerAccelerationChanged)

    Q_PROPERTY(bool supportsPointerAccelerationProfileFlat READ
                   supportsPointerAccelerationProfileFlat CONSTANT)
    Q_PROPERTY(bool defaultPointerAccelerationProfileFlat READ defaultPointerAccelerationProfileFlat
                   CONSTANT)
    Q_PROPERTY(bool pointerAccelerationProfileFlat READ pointerAccelerationProfileFlat WRITE
                   setPointerAccelerationProfileFlat NOTIFY pointerAccelerationProfileChanged)

    Q_PROPERTY(bool supportsPointerAccelerationProfileAdaptive READ
                   supportsPointerAccelerationProfileAdaptive CONSTANT)
    Q_PROPERTY(bool defaultPointerAccelerationProfileAdaptive READ
                   defaultPointerAccelerationProfileAdaptive CONSTANT)
    Q_PROPERTY(bool pointerAccelerationProfileAdaptive READ pointerAccelerationProfileAdaptive WRITE
                   setPointerAccelerationProfileAdaptive NOTIFY pointerAccelerationProfileChanged)

    //
    // Tapping
    Q_PROPERTY(int tapFingerCount READ tapFingerCount CONSTANT)
    Q_PROPERTY(bool tapToClickEnabledByDefault READ tapToClickEnabledByDefault CONSTANT)
    Q_PROPERTY(bool tapToClick READ isTapToClick WRITE setTapToClick NOTIFY tapToClickChanged)

    Q_PROPERTY(bool supportsLmrTapButtonMap READ supportsLmrTapButtonMap CONSTANT)
    Q_PROPERTY(bool lmrTapButtonMapEnabledByDefault READ lmrTapButtonMapEnabledByDefault CONSTANT)
    Q_PROPERTY(bool lmrTapButtonMap READ lmrTapButtonMap WRITE setLmrTapButtonMap NOTIFY
                   tapButtonMapChanged)

    Q_PROPERTY(bool tapAndDragEnabledByDefault READ tapAndDragEnabledByDefault CONSTANT)
    Q_PROPERTY(bool tapAndDrag READ isTapAndDrag WRITE setTapAndDrag NOTIFY tapAndDragChanged)
    Q_PROPERTY(bool tapDragLockEnabledByDefault READ tapDragLockEnabledByDefault CONSTANT)
    Q_PROPERTY(bool tapDragLock READ isTapDragLock WRITE setTapDragLock NOTIFY tapDragLockChanged)

    Q_PROPERTY(bool supportsMiddleEmulation READ supportsMiddleEmulation CONSTANT)
    Q_PROPERTY(bool middleEmulationEnabledByDefault READ middleEmulationEnabledByDefault CONSTANT)
    Q_PROPERTY(bool middleEmulation READ isMiddleEmulation WRITE setMiddleEmulation NOTIFY
                   middleEmulationChanged)

    //
    // Scrolling
    Q_PROPERTY(bool supportsNaturalScroll READ supportsNaturalScroll CONSTANT)
    Q_PROPERTY(bool naturalScrollEnabledByDefault READ naturalScrollEnabledByDefault CONSTANT)
    Q_PROPERTY(
        bool naturalScroll READ isNaturalScroll WRITE setNaturalScroll NOTIFY naturalScrollChanged)

    Q_PROPERTY(bool supportsScrollTwoFinger READ supportsScrollTwoFinger CONSTANT)
    Q_PROPERTY(bool scrollTwoFingerEnabledByDefault READ scrollTwoFingerEnabledByDefault CONSTANT)
    Q_PROPERTY(bool scrollTwoFinger READ isScrollTwoFinger WRITE setScrollTwoFinger NOTIFY
                   scrollMethodChanged)

    Q_PROPERTY(bool supportsScrollEdge READ supportsScrollEdge CONSTANT)
    Q_PROPERTY(bool scrollEdgeEnabledByDefault READ scrollEdgeEnabledByDefault CONSTANT)
    Q_PROPERTY(bool scrollEdge READ isScrollEdge WRITE setScrollEdge NOTIFY scrollMethodChanged)

    Q_PROPERTY(bool supportsScrollOnButtonDown READ supportsScrollOnButtonDown CONSTANT)
    Q_PROPERTY(
        bool scrollOnButtonDownEnabledByDefault READ scrollOnButtonDownEnabledByDefault CONSTANT)
    Q_PROPERTY(quint32 defaultScrollButton READ defaultScrollButton CONSTANT)
    Q_PROPERTY(bool scrollOnButtonDown READ isScrollOnButtonDown WRITE setScrollOnButtonDown NOTIFY
                   scrollMethodChanged)
    Q_PROPERTY(
        quint32 scrollButton READ scrollButton WRITE setScrollButton NOTIFY scrollButtonChanged)

    Q_PROPERTY(
        qreal scrollFactor READ scrollFactor WRITE setScrollFactor NOTIFY scrollFactorChanged)

    //
    // Switches
    Q_PROPERTY(bool switchDevice READ isSwitch CONSTANT)
    Q_PROPERTY(bool lidSwitch READ isLidSwitch CONSTANT)
    Q_PROPERTY(bool tabletModeSwitch READ isTabletModeSwitch CONSTANT)

    //
    // Click Methods
    Q_PROPERTY(bool supportsClickMethodAreas READ supportsClickMethodAreas CONSTANT)
    Q_PROPERTY(bool defaultClickMethodAreas READ defaultClickMethodAreas CONSTANT)
    Q_PROPERTY(bool clickMethodAreas READ isClickMethodAreas WRITE setClickMethodAreas NOTIFY
                   clickMethodChanged)

    Q_PROPERTY(bool supportsClickMethodClickfinger READ supportsClickMethodClickfinger CONSTANT)
    Q_PROPERTY(bool defaultClickMethodClickfinger READ defaultClickMethodClickfinger CONSTANT)
    Q_PROPERTY(bool clickMethodClickfinger READ isClickMethodClickfinger WRITE
                   setClickMethodClickfinger NOTIFY clickMethodChanged)

public:
    explicit device(input::control::keyboard* control, QObject* parent);
    explicit device(input::control::pointer* control, QObject* parent);
    explicit device(input::control::switch_device* control, QObject* parent);
    explicit device(input::control::touch* control, QObject* parent);
    ~device() override;

    bool isKeyboard() const;
    bool isAlphaNumericKeyboard() const;
    bool isPointer() const;
    bool isTouchpad() const;
    bool isTouch() const;
    bool isTabletTool() const;
    bool isTabletPad() const;
    bool supportsGesture() const;
    QString name() const;
    QString sysName() const;
    QString outputName() const;
    QSizeF size() const;
    quint32 product() const;
    quint32 vendor() const;
    Qt::MouseButtons supportedButtons() const;
    int tapFingerCount() const;
    bool tapToClickEnabledByDefault() const;
    bool isTapToClick() const;
    void setTapToClick(bool set);
    bool tapAndDragEnabledByDefault() const;
    bool isTapAndDrag() const;
    void setTapAndDrag(bool set);
    bool tapDragLockEnabledByDefault() const;
    bool isTapDragLock() const;
    void setTapDragLock(bool set);
    bool supportsDisableWhileTyping() const;
    bool disableWhileTypingEnabledByDefault() const;
    bool supportsPointerAcceleration() const;
    bool supportsLeftHanded() const;
    bool supportsCalibrationMatrix() const;
    bool supportsDisableEvents() const;
    bool supportsDisableEventsOnExternalMouse() const;
    bool supportsMiddleEmulation() const;
    bool supportsNaturalScroll() const;
    bool supportsScrollTwoFinger() const;
    bool supportsScrollEdge() const;
    bool supportsScrollOnButtonDown() const;
    bool leftHandedEnabledByDefault() const;
    bool middleEmulationEnabledByDefault() const;
    bool naturalScrollEnabledByDefault() const;
    bool scrollTwoFingerEnabledByDefault() const;
    bool scrollEdgeEnabledByDefault() const;
    bool scrollOnButtonDownEnabledByDefault() const;
    bool supportsLmrTapButtonMap() const;
    bool lmrTapButtonMapEnabledByDefault() const;

    void setLmrTapButtonMap(bool set);
    bool lmrTapButtonMap() const;

    quint32 defaultScrollButton() const;
    bool isMiddleEmulation() const;
    void setMiddleEmulation(bool set);
    bool isNaturalScroll() const;
    void setNaturalScroll(bool set);
    bool isScrollTwoFinger() const;
    void setScrollTwoFinger(bool set);
    bool isScrollEdge() const;
    void setScrollEdge(bool set);
    bool isScrollOnButtonDown() const;
    void setScrollOnButtonDown(bool set);
    quint32 scrollButton() const;
    void setScrollButton(quint32 button);

    qreal scrollFactorDefault() const;
    qreal scrollFactor() const;
    void setScrollFactor(qreal factor);

    void setDisableWhileTyping(bool set);
    bool isDisableWhileTyping() const;
    bool isLeftHanded() const;
    /**
     * Sets the Device to left handed mode if @p set is @c true.
     * If @p set is @c false the device is set to right handed mode
     */
    void setLeftHanded(bool set);

    qreal defaultPointerAcceleration() const;
    qreal pointerAcceleration() const;

    void setPointerAcceleration(qreal acceleration);
    bool supportsPointerAccelerationProfileFlat() const;
    bool supportsPointerAccelerationProfileAdaptive() const;
    bool defaultPointerAccelerationProfileFlat() const;
    bool defaultPointerAccelerationProfileAdaptive() const;
    bool pointerAccelerationProfileFlat() const;
    bool pointerAccelerationProfileAdaptive() const;
    void setPointerAccelerationProfileFlat(bool set);
    void setPointerAccelerationProfileAdaptive(bool set);
    bool supportsClickMethodAreas() const;
    bool defaultClickMethodAreas() const;
    bool isClickMethodAreas() const;
    bool supportsClickMethodClickfinger() const;
    bool defaultClickMethodClickfinger() const;
    bool isClickMethodClickfinger() const;
    void setClickMethodAreas(bool set);
    void setClickMethodClickfinger(bool set);

    bool isEnabled() const;
    void setEnabled(bool enabled);

    bool isSwitch() const;

    bool isLidSwitch() const;

    bool isTabletModeSwitch() const;

    input::control::device* dev{nullptr};
    input::control::keyboard* keyboard_ctrl{nullptr};
    input::control::pointer* pointer_ctrl{nullptr};
    input::control::switch_device* switch_ctrl{nullptr};
    input::control::touch* touch_ctrl{nullptr};

    std::string sys_name;

Q_SIGNALS:
    void tapButtonMapChanged();
    void leftHandedChanged();
    void disableWhileTypingChanged();
    void pointerAccelerationChanged();
    void pointerAccelerationProfileChanged();
    void enabledChanged();
    void tapToClickChanged();
    void tapAndDragChanged();
    void tapDragLockChanged();
    void middleEmulationChanged();
    void naturalScrollChanged();
    void scrollMethodChanged();
    void scrollButtonChanged();
    void scrollFactorChanged();
    void clickMethodChanged();
};

}

Q_DECLARE_METATYPE(KWin::input::dbus::device*)
