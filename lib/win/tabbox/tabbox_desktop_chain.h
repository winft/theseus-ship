/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_TABBOX_DESKTOP_CHAIN_H
#define KWIN_TABBOX_DESKTOP_CHAIN_H

#include "kwin_export.h"
#include <win/subspace.h>

#include <QHash>
#include <QObject>
#include <QVector>

namespace KWin
{
namespace win
{

/**
 * @brief A chain for last recently used virtual desktops.
 */
class KWIN_EXPORT tabbox_desktop_chain
{
public:
    /**
     * Creates a last recently used virtual desktop chain with the given @p initialSize.
     */
    explicit tabbox_desktop_chain(uint initial_size = 0);
    /**
     * Returns the next desktop in the chain starting from @p index_desktop.
     * In case that the @p index_desktop is the last desktop of the chain, the method wraps around
     * and returns the first desktop stored in the chain.
     * In case the chain is valid, but does not contain the @p index_desktop, the first element of
     * the chain is returned.
     * In case the chain is not valid, the always valid virtual desktop with identifier @c 1
     * is returned.
     * @param index_desktop The id of the virtual desktop which should be used as a starting point
     * @return The next virtual desktop in the chain
     */
    uint next(uint index_desktop) const;
    /**
     * Adds the @p desktop to the chain. The @p desktop becomes the first element of the
     * chain. All desktops in the chain from the previous index of @p desktop are moved
     * one position in the chain.
     * @param desktop The new desktop to be the top most element in the chain.
     */
    void add(uint desktop);
    /**
     * Resizes the chain from @p previous_size to @p new_size.
     * In case the chain grows new elements are added with a meaning full id in the range
     * [previous_size, new_size].
     * In case the chain shrinks it is ensured that no element points to a virtual desktop
     * with an id larger than @p newSize.
     * @param previous_size The previous size of the desktop chain
     * @param new_size The size to be used for the desktop chain
     */
    void resize(uint previous_size, uint new_size);

private:
    /**
     * Initializes the chain with default values.
     */
    void init();
    QVector<uint> m_chain;
};

/**
 * @brief A manager for multiple desktop chains.
 *
 * This manager keeps track of multiple desktop chains which have a given identifier.
 * A common usage for this is to have a different desktop chain for each Activity.
 */
class KWIN_EXPORT tabbox_desktop_chain_manager : public QObject
{
    Q_OBJECT

public:
    explicit tabbox_desktop_chain_manager(QObject* parent = nullptr);
    ~tabbox_desktop_chain_manager() override;

    /**
     * Returns the next virtual desktop starting from @p index_desktop in the currently used chain.
     * @param index_desktop The id of the virtual desktop which should be used as a starting point
     * @return The next virtual desktop in the currently used chain
     * @see desktop_chain::next
     */
    uint next(uint index_desktop) const;

public Q_SLOTS:
    /**
     * Adds the @p curren_dDesktop to the currently used desktop chain.
     * @param previous_desktop The previously used desktop, should be the top element of the chain
     * @param current_desktop The desktop which should be the new top element of the chain
     */
    void add_desktop(win::subspace* prev, win::subspace* next);
    /**
     * Resizes all managed desktop chains from @p previous_size to @p new_size.
     * @param previous_size The previously used size for the chains
     * @param new_size The size to be used for the chains
     * @see desktop_chain::resize
     */
    void resize(uint previous_size, uint new_size);
    /**
     * Switches to the desktop chain identified by the given @p identifier.
     * If there is no chain yet for the given @p identifier a new chain is created and used.
     * @param identifier The identifier of the desktop chain to be used
     */
    void use_chain(const QString& identifier);

private:
    typedef QHash<QString, tabbox_desktop_chain> tabbox_desktop_chains;
    /**
     * Creates a new desktop chain for the given @p identifier and adds it to the list
     * of identifiers.
     * @returns Position of the new chain in the managed list of chains
     */
    tabbox_desktop_chains::Iterator add_new_chain(const QString& identifier);
    /**
     * Creates the very first list to be used when an @p identifier comes in.
     * The dummy chain which is used by default gets copied and used for this chain.
     */
    void create_first_chain(const QString& identifier);

    tabbox_desktop_chains::Iterator m_current_chain;
    tabbox_desktop_chains m_chains;
    /**
     * The maximum size to be used for a new desktop chain
     */
    uint m_max_chain_size;
};

} // win
} // namespace KWin

#endif
