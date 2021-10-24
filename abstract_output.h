/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

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
#ifndef KWIN_ABSTRACT_OUTPUT_H
#define KWIN_ABSTRACT_OUTPUT_H

#include <kwin_export.h>

#include <QObject>
#include <QRect>
#include <QSize>
#include <QVector>

namespace Wrapland
{
namespace Server
{
class OutputChangesetV1;
}
}

namespace KWin
{

class KWIN_EXPORT GammaRamp
{
public:
    GammaRamp(uint32_t size);

    /**
     * Returns the size of the gamma ramp.
     */
    uint32_t size() const;

    /**
     * Returns pointer to the first red component in the gamma ramp.
     *
     * The returned pointer can be used for altering the red component
     * in the gamma ramp.
     */
    uint16_t* red();

    /**
     * Returns pointer to the first red component in the gamma ramp.
     */
    uint16_t const* red() const;

    /**
     * Returns pointer to the first green component in the gamma ramp.
     *
     * The returned pointer can be used for altering the green component
     * in the gamma ramp.
     */
    uint16_t* green();

    /**
     * Returns pointer to the first green component in the gamma ramp.
     */
    uint16_t const* green() const;

    /**
     * Returns pointer to the first blue component in the gamma ramp.
     *
     * The returned pointer can be used for altering the blue component
     * in the gamma ramp.
     */
    uint16_t* blue();

    /**
     * Returns pointer to the first blue component in the gamma ramp.
     */
    uint16_t const* blue() const;

private:
    QVector<uint16_t> m_table;
    uint32_t m_size;
};

/**
 * Generic output representation.
 */
class KWIN_EXPORT AbstractOutput : public QObject
{
    Q_OBJECT

public:
    explicit AbstractOutput(QObject* parent = nullptr);
    ~AbstractOutput() override;

    enum class DpmsMode { On, Standby, Suspend, Off };

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
     * This sets the changes and tests them against the specific output.
     *
     * Default implementation does nothing
     */
    virtual void apply_changes(Wrapland::Server::OutputChangesetV1 const* changeset);

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
    virtual bool set_gamma_ramp(GammaRamp const& gamma);

    virtual void update_dpms(DpmsMode mode);
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
    Q_DISABLE_COPY(AbstractOutput)
};

} // namespace KWin

#endif
