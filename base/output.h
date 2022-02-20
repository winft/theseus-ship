/*
    SPDX-FileCopyrightText: 2019, 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <QRect>
#include <QSize>
#include <QVector>

namespace KWin
{

class gamma_ramp;

namespace base
{

enum class dpms_mode {
    on,
    standby,
    suspend,
    off,
};

class KWIN_EXPORT output : public QObject
{
    Q_OBJECT

public:
    output() = default;

    /**
     * Returns the name of this output.
     */
    virtual QString name() const = 0;

    /**
     * Enable or disable the output.
     *
     * Default implementation does nothing
     */
    virtual void set_enabled(bool enable);

    /**
     * Returns geometry of this output in device independent pixels.
     */
    virtual QRect geometry() const = 0;

    /**
     * Returns the approximate vertical refresh rate of this output, in mHz.
     */
    virtual int refresh_rate() const = 0;

    /**
     * Returns whether this output is connected through an internal connector,
     * e.g. LVDS, or eDP.
     *
     * Default implementation returns @c false.
     */
    virtual bool is_internal() const;

    /**
     * Returns the ratio between physical pixels and logical pixels.
     *
     * Default implementation returns 1.
     */
    virtual qreal scale() const;

    /**
     * Returns the physical size of this output, in millimeters.
     *
     * Default implementation returns an invalid QSize.
     */
    virtual QSize physical_size() const;

    /**
     * Returns the size of the gamma lookup table.
     *
     * Default implementation returns 0.
     */
    virtual int gamma_ramp_size() const;

    /**
     * Sets the gamma ramp of this output.
     *
     * Returns @c true if the gamma ramp was successfully set.
     */
    virtual bool set_gamma_ramp(gamma_ramp const& gamma);

    virtual void update_dpms(dpms_mode mode);
    virtual bool is_dpms_on() const
    {
        return true;
    }

    /** Returns the resolution of the output.  */
    virtual QSize pixel_size() const
    {
        return geometry().size();
    }

    /**
     * Returns the manufacturer of the screen.
     */
    virtual QString manufacturer() const
    {
        return QString();
    }
    /**
     * Returns the model of the screen.
     */
    virtual QString model() const
    {
        return QString();
    }
    /**
     * Returns the serial number of the screen.
     */
    virtual QString serial_number() const
    {
        return QString();
    }

Q_SIGNALS:
    /**
     * This signal is emitted when the geometry of this output has changed.
     */
    void geometry_changed();
    /**
     * This signal is emitted when the output has been enabled or disabled.
     */
    void enabled_changed();
    /**
     * This signal is emitted when the device pixel ratio of the output has changed.
     */
    void scale_changed();

    /**
     * Notifies that the display will be dimmed in @p time ms. This allows
     * effects to plan for it and hopefully animate it
     */
    void about_to_turn_off(std::chrono::milliseconds time);

    /**
     * Notifies that the output has been turned on and the wake can be decorated.
     */
    void wake_up();

private:
    Q_DISABLE_COPY(output)
};

}
}

Q_DECLARE_METATYPE(KWin::base::output const*);
