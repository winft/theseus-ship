/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wrapper.h"

#include <xcb/xcb.h>

namespace KWin::base::x11::xcb
{

XCB_WRAPPER_DATA(property_data,
                 xcb_get_property,
                 uint8_t,
                 xcb_window_t,
                 xcb_atom_t,
                 xcb_atom_t,
                 uint32_t,
                 uint32_t)
class property : public wrapper<property_data,
                                uint8_t,
                                xcb_window_t,
                                xcb_atom_t,
                                xcb_atom_t,
                                uint32_t,
                                uint32_t>
{
public:
    explicit property(xcb_connection_t* con)
        : wrapper<property_data, uint8_t, xcb_window_t, xcb_atom_t, xcb_atom_t, uint32_t, uint32_t>(
            con)
        , m_type(XCB_ATOM_NONE)
    {
    }
    property(property const& other)
        : wrapper<property_data, uint8_t, xcb_window_t, xcb_atom_t, xcb_atom_t, uint32_t, uint32_t>(
            other)
        , m_type(other.m_type)
    {
    }
    property(xcb_connection_t* con,
             uint8_t _delete,
             xcb_window_t window,
             xcb_atom_t prop,
             xcb_atom_t type,
             uint32_t long_offset,
             uint32_t long_length)
        : wrapper<property_data, uint8_t, xcb_window_t, xcb_atom_t, xcb_atom_t, uint32_t, uint32_t>(
            con,
            window,
            _delete,
            window,
            prop,
            type,
            long_offset,
            long_length)
        , m_type(type)
    {
    }
    property& operator=(property const& other)
    {
        wrapper<property_data, uint8_t, xcb_window_t, xcb_atom_t, xcb_atom_t, uint32_t, uint32_t>::
        operator=(other);
        m_type = other.m_type;
        return *this;
    }

    /**
     * @brief Overloaded method for convenience.
     *
     * Uses the type which got passed into the ctor and derives the format from the sizeof(T).
     * Note: for the automatic format detection the size of the type T may not vary between
     * architectures. Thus one needs to use e.g. uint32_t instead of long. In general all xcb
     * data types can be used, all Xlib data types can not be used.
     *
     * @param defaultValue The default value to return in case of error
     * @param ok Set to @c false in case of error, @c true in case of success
     * @return The read value or @p defaultValue in error case
     */
    template<typename T>
    inline typename std::enable_if<!std::is_pointer<T>::value, T>::type value(T defaultValue = T(),
                                                                              bool* ok = nullptr)
    {
        return value<T>(sizeof(T) * 8, m_type, defaultValue, ok);
    }
    /**
     * @brief Reads the property as a POD type.
     *
     * Returns the first value of the property data. In case of @p format or @p type mismatch
     * the @p defaultValue is returned. The optional argument @p ok is set
     * to @c false in case of error and to @c true in case of successful reading of
     * the property.
     *
     * @param format The expected format of the property value, e.g. 32 for XCB_ATOM_CARDINAL
     * @param type The expected type of the property value, e.g. XCB_ATOM_CARDINAL
     * @param defaultValue The default value to return in case of error
     * @param ok Set to @c false in case of error, @c true in case of success
     * @return The read value or @p defaultValue in error case
     */
    template<typename T>
    inline typename std::enable_if<!std::is_pointer<T>::value, T>::type
    value(uint8_t format, xcb_atom_t type, T defaultValue = T(), bool* ok = nullptr)
    {
        T* reply = value<T*>(format, type, nullptr, ok);
        if (!reply) {
            return defaultValue;
        }
        return reply[0];
    }
    /**
     * @brief Overloaded method for convenience.
     *
     * Uses the type which got passed into the ctor and derives the format from the sizeof(T).
     * Note: for the automatic format detection the size of the type T may not vary between
     * architectures. Thus one needs to use e.g. uint32_t instead of long. In general all xcb
     * data types can be used, all Xlib data types can not be used.
     *
     * @param defaultValue The default value to return in case of error
     * @param ok Set to @c false in case of error, @c true in case of success
     * @return The read value or @p defaultValue in error case
     */
    template<typename T>
    inline typename std::enable_if<std::is_pointer<T>::value, T>::type
    value(T defaultValue = nullptr, bool* ok = nullptr)
    {
        return value<T>(
            sizeof(typename std::remove_pointer<T>::type) * 8, m_type, defaultValue, ok);
    }
    /**
     * @brief Reads the property as an array of T.
     *
     * This method is an overload for the case that T is a pointer type.
     *
     * Return the property value casted to the pointer type T. In case of @p format
     * or @p type mismatch the @p defaultValue is returned. Also if the value length
     * is @c 0 the @p defaultValue is returned. The optional argument @p ok is set
     * to @c false in case of error and to @c true in case of successful reading of
     * the property. Ok will always be true if the property exists and has been
     * successfully read, even in the case the property is empty and its length is 0
     *
     * @param format The expected format of the property value, e.g. 32 for XCB_ATOM_CARDINAL
     * @param type The expected type of the property value, e.g. XCB_ATOM_CARDINAL
     * @param defaultValue The default value to return in case of error
     * @param ok Set to @c false in case of error, @c true in case of success
     * @return The read value or @p defaultValue in error case
     */
    template<typename T>
    inline typename std::enable_if<std::is_pointer<T>::value, T>::type
    value(uint8_t format, xcb_atom_t type, T defaultValue = nullptr, bool* ok = nullptr)
    {
        if (ok) {
            *ok = false;
        }
        property_data::reply_type const* reply = data();
        if (!reply) {
            return defaultValue;
        }
        if (reply->type != type) {
            return defaultValue;
        }
        if (reply->format != format) {
            return defaultValue;
        }

        if (ok) {
            *ok = true;
        }
        if (xcb_get_property_value_length(reply) == 0) {
            return defaultValue;
        }

        return reinterpret_cast<T>(xcb_get_property_value(reply));
    }
    /**
     * @brief Reads the property as string and returns a QByteArray.
     *
     * In case of error this method returns a null QByteArray.
     */
    inline QByteArray
    to_byte_array(uint8_t format = 8, xcb_atom_t type = XCB_ATOM_STRING, bool* ok = nullptr)
    {
        bool valueOk = false;
        const char* reply = value<const char*>(format, type, nullptr, &valueOk);
        if (ok) {
            *ok = valueOk;
        }

        if (valueOk && !reply) {
            // valid, not null, but empty data
            return QByteArray("", 0);
        } else if (!valueOk) {
            // Property not found, data empty and null
            return QByteArray();
        }
        return QByteArray(reply, xcb_get_property_value_length(data()));
    }
    /**
     * @brief Overloaded method for convenience.
     */
    inline QByteArray to_byte_array(bool* ok)
    {
        return to_byte_array(8, m_type, ok);
    }
    /**
     * @brief Reads the property as a boolean value.
     *
     * If the property reply length is @c 1 the first element is interpreted as a boolean
     * value returning @c true for any value unequal to @c 0 and @c false otherwise.
     *
     * In case of error this method returns @c false. Thus it is not possible to distinguish
     * between error case and a read @c false value. Use the optional argument @p ok to
     * distinguish the error case.
     *
     * @param format Expected format. Defaults to 32.
     * @param type Expected type Defaults to XCB_ATOM_CARDINAL.
     * @param ok Set to @c false in case of error, @c true in case of success
     * @return bool The first element interpreted as a boolean value or @c false in error case
     * @see value
     */
    inline bool
    to_bool(uint8_t format = 32, xcb_atom_t type = XCB_ATOM_CARDINAL, bool* ok = nullptr)
    {
        bool* reply = value<bool*>(format, type, nullptr, ok);
        if (!reply) {
            return false;
        }
        if (data()->value_len != 1) {
            if (ok) {
                *ok = false;
            }
            return false;
        }
        return reply[0] != 0;
    }
    /**
     * @brief Overloaded method for convenience.
     */
    inline bool to_bool(bool* ok)
    {
        return to_bool(32, m_type, ok);
    }

private:
    xcb_atom_t m_type;
};

class string_property : public property
{
public:
    explicit string_property(xcb_connection_t* con)
        : property(con)
    {
    }

    string_property(xcb_connection_t* con, xcb_window_t w, xcb_atom_t p)
        : property(con, false, w, p, XCB_ATOM_STRING, 0, 10000)
    {
    }
    operator QByteArray()
    {
        return to_byte_array();
    }
};

class transient_for : public property
{
public:
    transient_for(xcb_connection_t* con, xcb_window_t window)
        : property(con, 0, window, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 0, 1)
    {
    }

    /**
     * @brief Fill given window pointer with the WM_TRANSIENT_FOR property of a window.
     * @param prop WM_TRANSIENT_FOR property value.
     * @returns @c true on success, @c false otherwise
     */
    inline bool get_transient_for(xcb_window_t* prop)
    {
        auto windows = value<xcb_window_t*>();
        if (!windows) {
            return false;
        }

        *prop = *windows;
        return true;
    }
};

}
