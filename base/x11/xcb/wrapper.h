/*
    SPDX-FileCopyrightText: 2012, 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"

#include <xcb/xcb.h>

namespace KWin::base::x11::xcb
{

/**
 * @brief Variadic template to wrap an xcb request.
 *
 * This struct is part of the generic implementation to wrap xcb requests
 * and fetching their reply. Each request is represented by two templated
 * elements: wrapper_data and wrapper.
 *
 * The wrapper_data defines the following types:
 * @li reply_type of the xcb request
 * @li cookie_type of the xcb request
 * @li function pointer type for the xcb request
 * @li function pointer type for the reply
 * This uses variadic template arguments thus it can be used to specify any
 * xcb request.
 *
 * As the wrapper_data does not specify the actual function pointers one needs
 * to derive another struct which specifies the function pointer requestFunc and
 * the function pointer replyFunc as static constexpr of type reply_func and
 * reply_type respectively. E.g. for the command xcb_get_geometry:
 * @code
 * struct geometry_data : public wrapper_data< xcb_get_geometry_reply_t, xcb_get_geometry_cookie_t,
 * xcb_drawable_t >
 * {
 *    static constexpr request_func requestFunc = &xcb_get_geometry_unchecked;
 *    static constexpr reply_func replyFunc = &xcb_get_geometry_reply;
 * };
 * @endcode
 *
 * To simplify this definition the macro XCB_WRAPPER_DATA is provided.
 * For the same xcb command this looks like this:
 * @code
 * XCB_WRAPPER_DATA(geometry_data, xcb_get_geometry, xcb_drawable_t)
 * @endcode
 *
 * The derived wrapper_data has to be passed as first template argument to wrapper. The other
 * template arguments of wrapper are the same variadic template arguments as passed into
 * wrapper_data. This is ensured at compile time and will cause a compile error in case there
 * is a mismatch of the variadic template arguments passed to wrapper_data and wrapper.
 * Passing another type than a struct derived from wrapper_data to wrapper will result in a
 * compile error. The following code snippets won't compile:
 * @code
 * XCB_WRAPPER_DATA(geometry_data, xcb_get_geometry, xcb_drawable_t)
 * // fails with "static assertion failed: Argument miss-match between wrapper and wrapper_data"
 * class IncorrectArguments : public wrapper<geometry_data, uint8_t>
 * {
 * public:
 *     IncorrectArguments() = default;
 *     IncorrectArguments(xcb_window_t window) : wrapper<geometry_data, uint8_t>(window) {}
 * };
 *
 * // fails with "static assertion failed: Data template argument must be derived from wrapper_data"
 * class wrapper_dataDirectly : public wrapper<wrapper_data<xcb_get_geometry_reply_t,
 * xcb_get_geometry_request_t, xcb_drawable_t>, xcb_drawable_t>
 * {
 * public:
 *     wrapper_dataDirectly() = default;
 *     wrapper_dataDirectly(xcb_window_t window) : wrapper<wrapper_data<xcb_get_geometry_reply_t,
 * xcb_get_geometry_request_t, xcb_drawable_t>, xcb_drawable_t>(window) {}
 * };
 *
 * // fails with "static assertion failed: Data template argument must be derived from wrapper_data"
 * struct Fakewrapper_data
 * {
 *     typedef xcb_get_geometry_reply_t reply_type;
 *     typedef xcb_get_geometry_cookie_t cookie_type;
 *     typedef std::tuple<xcb_drawable_t> argument_types;
 *     typedef cookie_type (*request_func)(xcb_connection_t*, xcb_drawable_t);
 *     typedef reply_type *(*reply_func)(xcb_connection_t*, cookie_type, xcb_generic_error_t**);
 *     static constexpr std::size_t argumentCount = 1;
 *     static constexpr request_func requestFunc = &xcb_get_geometry_unchecked;
 *     static constexpr reply_func replyFunc = &xcb_get_geometry_reply;
 * };
 * class NotDerivedFromwrapper_data : public wrapper<Fakewrapper_data, xcb_drawable_t>
 * {
 * public:
 *     NotDerivedFromwrapper_data() = default;
 *     NotDerivedFromwrapper_data(xcb_window_t window) : wrapper<Fakewrapper_data,
 * xcb_drawable_t>(window) {}
 * };
 * @endcode
 *
 * The wrapper provides an easy to use RAII API which calls the wrapper_data's requestFunc in
 * the ctor and fetches the reply the first time it is used. In addition the dtor takes care
 * of freeing the reply if it got fetched, otherwise it discards the reply. The wrapper can
 * be used as if it were the reply_type directly.
 *
 * There are several command wrappers defined which either subclass wrapper to add methods to
 * simplify the usage of the result_type or use a typedef. To add a new typedef one can use the
 * macro XCB_WRAPPER which creates the wrapper_data struct as XCB_WRAPPER_DATA does and the
 * typedef. E.g:
 * @code
 * XCB_WRAPPER(Geometry, xcb_get_geometry, xcb_drawable_t)
 * @endcode
 *
 * creates a typedef Geometry and the struct geometry_data.
 *
 * Overall this allows to simplify the Xcb usage. For example consider the
 * following xcb code snippet:
 * @code
 * xcb_window_t w; // some window
 * xcb_connection_t *c = connection();
 * const xcb_get_geometry_cookie_t cookie = xcb_get_geometry_unchecked(c, w);
 * // do other stuff
 * xcb_get_geometry_reply_t *reply = xcb_get_geometry_reply(c, cookie, nullptr);
 * if (reply) {
 *     reply->x; // do something with the geometry
 * }
 * free(reply);
 * @endcode
 *
 * With the help of the wrapper class this can be simplified to:
 * @code
 * xcb_window_t w; // some window
 * xcb::Geometry geo(w);
 * if (!geo.is_null()) {
 *     geo->x; // do something with the geometry
 * }
 * @endcode
 *
 * @see XCB_WRAPPER_DATA
 * @see XCB_WRAPPER
 * @see wrapper
 * @see window_attributes
 * @see overlay_window
 * @see window_geometry
 * @see tree
 * @see input_focus
 * @see transient_for
 */
template<typename Reply, typename Cookie, typename... Args>
struct wrapper_data {
    /**
     * @brief The type returned by the xcb reply function.
     */
    typedef Reply reply_type;
    /**
     * @brief The type returned by the xcb request function.
     */
    typedef Cookie cookie_type;
    /**
     * @brief Variadic arguments combined as a std::tuple.
     * @internal Used for verifying the arguments.
     */
    typedef std::tuple<Args...> argument_types;
    /**
     * @brief The function pointer definition for the xcb request function.
     */
    typedef Cookie (*request_func)(xcb_connection_t*, Args...);
    /**
     * @brief The function pointer definition for the xcb reply function.
     */
    typedef Reply* (*reply_func)(xcb_connection_t*, Cookie, xcb_generic_error_t**);
    /**
     * @brief Number of variadic arguments.
     * @internal Used for verifying the arguments.
     */
    static constexpr std::size_t argumentCount = sizeof...(Args);
};

/**
 * @brief Partial template specialization for wrapper_data with no further arguments.
 *
 * This will be used for xcb requests just taking the xcb_connection_t* argument.
 */
template<typename Reply, typename Cookie>
struct wrapper_data<Reply, Cookie> {
    typedef Reply reply_type;
    typedef Cookie cookie_type;
    typedef std::tuple<> argument_types;
    typedef Cookie (*request_func)(xcb_connection_t*);
    typedef Reply* (*reply_func)(xcb_connection_t*, Cookie, xcb_generic_error_t**);
    static constexpr std::size_t argumentCount = 0;
};

/**
 * @brief Abstract base class for the wrapper.
 *
 * This class contains the complete functionality of the wrapper. It's only an abstract
 * base class to provide partial template specialization for more specific constructors.
 */
template<typename Data>
class abstract_wrapper
{
public:
    using Cookie = typename Data::cookie_type;
    using Reply = typename Data::reply_type;

    virtual ~abstract_wrapper()
    {
        cleanup();
    }
    inline abstract_wrapper& operator=(const abstract_wrapper& other)
    {
        if (this != &other) {
            // if we had managed a reply, free it
            cleanup();
            // copy members
            m_retrieved = other.m_retrieved;
            m_cookie = other.m_cookie;
            m_window = other.m_window;
            m_reply = other.m_reply;
            // take over the responsibility for the reply pointer
            take_from_other(const_cast<abstract_wrapper&>(other));
        }
        return *this;
    }

    inline const Reply* operator->()
    {
        get_reply();
        return m_reply;
    }
    inline bool is_null()
    {
        get_reply();
        return m_reply == nullptr;
    }
    inline bool is_null() const
    {
        const_cast<abstract_wrapper*>(this)->get_reply();
        return m_reply == NULL;
    }
    inline operator bool()
    {
        return !is_null();
    }
    inline operator bool() const
    {
        return !is_null();
    }
    inline const Reply* data()
    {
        get_reply();
        return m_reply;
    }
    inline const Reply* data() const
    {
        const_cast<abstract_wrapper*>(this)->get_reply();
        return m_reply;
    }
    inline xcb_window_t window() const
    {
        return m_window;
    }
    inline bool is_retrieved() const
    {
        return m_retrieved;
    }
    /**
     * Returns the value of the reply pointer referenced by this object. The reply pointer of
     * this object will be reset to null. Calling any method which requires the reply to be valid
     * will crash.
     *
     * Callers of this function take ownership of the pointer.
     */
    inline Reply* take()
    {
        get_reply();
        Reply* ret = m_reply;
        m_reply = nullptr;
        m_window = XCB_WINDOW_NONE;
        return ret;
    }

protected:
    abstract_wrapper()
        : m_retrieved(false)
        , m_window(XCB_WINDOW_NONE)
        , m_reply(nullptr)
    {
        m_cookie.sequence = 0;
    }
    explicit abstract_wrapper(xcb_window_t window, Cookie cookie)
        : m_retrieved(false)
        , m_cookie(cookie)
        , m_window(window)
        , m_reply(nullptr)
    {
    }
    explicit abstract_wrapper(const abstract_wrapper& other)
        : m_retrieved(other.m_retrieved)
        , m_cookie(other.m_cookie)
        , m_window(other.m_window)
        , m_reply(nullptr)
    {
        take_from_other(const_cast<abstract_wrapper&>(other));
    }
    void get_reply()
    {
        if (m_retrieved || !m_cookie.sequence) {
            return;
        }
        m_reply = Data::replyFunc(connection(), m_cookie, nullptr);
        m_retrieved = true;
    }

private:
    inline void cleanup()
    {
        if (!m_retrieved && m_cookie.sequence) {
            xcb_discard_reply(connection(), m_cookie.sequence);
        } else if (m_reply) {
            free(m_reply);
        }
    }
    inline void take_from_other(abstract_wrapper& other)
    {
        if (m_retrieved) {
            m_reply = other.take();
        } else {
            // ensure that other object doesn't try to get the reply or discards it in the dtor
            other.m_retrieved = true;
            other.m_window = XCB_WINDOW_NONE;
        }
    }

    bool m_retrieved;
    Cookie m_cookie;
    xcb_window_t m_window;
    Reply* m_reply;
};

/**
 * @brief Template to compare the arguments of two std::tuple.
 *
 * @internal Used by static_assert in wrapper
 */
template<typename T1, typename T2, std::size_t I>
struct tupleCompare {
    typedef typename std::tuple_element<I, T1>::type tuple1Type;
    typedef typename std::tuple_element<I, T2>::type tuple2Type;
    /**
     * @c true if both tuple have the same arguments, @c false otherwise.
     */
    static constexpr bool value
        = std::is_same<tuple1Type, tuple2Type>::value && tupleCompare<T1, T2, I - 1>::value;
};

/**
 * @brief Recursive template case for first tuple element.
 */
template<typename T1, typename T2>
struct tupleCompare<T1, T2, 0> {
    typedef typename std::tuple_element<0, T1>::type tuple1Type;
    typedef typename std::tuple_element<0, T2>::type tuple2Type;
    static constexpr bool value = std::is_same<tuple1Type, tuple2Type>::value;
};

/**
 * @brief wrapper taking a wrapper_data as first template argument and xcb request args as variadic
 * args.
 */
template<typename Data, typename... Args>
class wrapper : public abstract_wrapper<Data>
{
public:
    static_assert(!std::is_same<Data,
                                xcb::wrapper_data<typename Data::reply_type,
                                                  typename Data::cookie_type,
                                                  Args...>>::value,
                  "Data template argument must be derived from wrapper_data");
    static_assert(
        std::is_base_of<
            xcb::wrapper_data<typename Data::reply_type, typename Data::cookie_type, Args...>,
            Data>::value,
        "Data template argument must be derived from wrapper_data");
    static_assert(sizeof...(Args) == Data::argumentCount,
                  "Wrapper and wrapper_data need to have same template argument count");
    static_assert(tupleCompare<std::tuple<Args...>,
                               typename Data::argument_types,
                               sizeof...(Args) - 1>::value,
                  "Argument miss-match between Wrapper and wrapper_data");

    wrapper() = default;
    explicit wrapper(Args... args)
        : abstract_wrapper<Data>(XCB_WINDOW_NONE, Data::requestFunc(connection(), args...))
    {
    }
    explicit wrapper(xcb_window_t w, Args... args)
        : abstract_wrapper<Data>(w, Data::requestFunc(connection(), args...))
    {
    }
};

/**
 * @brief Template specialization for xcb_window_t being first variadic argument.
 */
template<typename Data, typename... Args>
class wrapper<Data, xcb_window_t, Args...> : public abstract_wrapper<Data>
{
public:
    static_assert(!std::is_same<Data,
                                xcb::wrapper_data<typename Data::reply_type,
                                                  typename Data::cookie_type,
                                                  xcb_window_t,
                                                  Args...>>::value,
                  "Data template argument must be derived from wrapper_data");
    static_assert(std::is_base_of<xcb::wrapper_data<typename Data::reply_type,
                                                    typename Data::cookie_type,
                                                    xcb_window_t,
                                                    Args...>,
                                  Data>::value,
                  "Data template argument must be derived from wrapper_data");
    static_assert(sizeof...(Args) + 1 == Data::argumentCount,
                  "wrapper and wrapper_data need to have same template argument count");
    static_assert(tupleCompare<std::tuple<xcb_window_t, Args...>,
                               typename Data::argument_types,
                               sizeof...(Args)>::value,
                  "Argument miss-match between wrapper and wrapper_data");

    wrapper() = default;
    explicit wrapper(xcb_window_t w, Args... args)
        : abstract_wrapper<Data>(w, Data::requestFunc(connection(), w, args...))
    {
    }
};

/**
 * @brief Template specialization for no variadic arguments.
 *
 * It's needed to prevent ambiguous constructors being generated.
 */
template<typename Data>
class wrapper<Data> : public abstract_wrapper<Data>
{
public:
    static_assert(!std::is_same<Data,
                                xcb::wrapper_data<typename Data::reply_type,
                                                  typename Data::cookie_type>>::value,
                  "Data template argument must be derived from wrapper_data");
    static_assert(
        std::is_base_of<xcb::wrapper_data<typename Data::reply_type, typename Data::cookie_type>,
                        Data>::value,
        "Data template argument must be derived from wrapper_data");
    static_assert(Data::argumentCount == 0,
                  "wrapper for no arguments constructed with wrapper_data with arguments");

    explicit wrapper()
        : abstract_wrapper<Data>(XCB_WINDOW_NONE, Data::requestFunc(connection()))
    {
    }
};

/**
 * @brief Macro to create the wrapper_data subclass.
 *
 * Creates a struct with name @p __NAME__ for the xcb request identified by @p __REQUEST__.
 * The variadic arguments are used to pass as template arguments to the wrapper_data.
 *
 * The @p __REQUEST__ is the common prefix of the cookie type, reply type, request function and
 * reply function. E.g. "xcb_get_geometry" is used to create:
 * @li cookie type xcb_get_geometry_cookie_t
 * @li reply type xcb_get_geometry_reply_t
 * @li request function pointer xcb_get_geometry_unchecked
 * @li reply function pointer xcb_get_geometry_reply
 *
 * @param __NAME__ The name of the wrapper_data subclass
 * @param __REQUEST__ The name of the xcb request, e.g. xcb_get_geometry
 * @param __VA_ARGS__ The variadic template arguments, e.g. xcb_drawable_t
 * @see XCB_WRAPPER
 */
#define XCB_WRAPPER_DATA(__NAME__, __REQUEST__, ...)                                               \
    struct __NAME__                                                                                \
        : public wrapper_data<__REQUEST__##_reply_t, __REQUEST__##_cookie_t, __VA_ARGS__> {        \
        static constexpr request_func requestFunc = &__REQUEST__##_unchecked;                      \
        static constexpr reply_func replyFunc = &__REQUEST__##_reply;                              \
    };

/**
 * @brief Macro to create wrapper typedef and wrapper_data.
 *
 * This macro expands the XCB_WRAPPER_DATA macro and creates an additional
 * typedef for Wrapper with name @p __NAME__. The created wrapper_data is also derived
 * from @p __NAME__ with "Data" as suffix.
 *
 * @param __NAME__ The name for the wrapper typedef
 * @param __REQUEST__ The name of the xcb request, passed to XCB_WRAPPER_DATA
 * @param __VA_ARGS__ The variadic template arguments for wrapper and wrapper_data
 * @see XCB_WRAPPER_DATA
 */
#define XCB_WRAPPER(__NAME__, __REQUEST__, ...)                                                    \
    XCB_WRAPPER_DATA(__NAME__##Data, __REQUEST__, __VA_ARGS__)                                     \
    typedef wrapper<__NAME__##Data, __VA_ARGS__> __NAME__;

}
