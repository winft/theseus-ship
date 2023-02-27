/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <stdlib.h>

namespace KWin::win::x11::net
{

template<class Z>
class rarray
{
public:
    /// Constructs an empty (size == 0) array.
    rarray()
        : sz(0)
        , capacity(2)
    {
        // allocate 2 elts and set to zero
        d = (Z*)calloc(capacity, sizeof(Z));
    }

    ~rarray()
    {
        free(d);
    }

    /// If the index is larger than the current size of the array, it is resized.
    Z& operator[](int index)
    {
        if (index >= capacity) {
            // allocate space for the new data
            // open table has amortized O(1) access time
            // when N elements appended consecutively -- exa
            int newcapacity = 2 * capacity > index + 1 ? 2 * capacity : index + 1; // max
            // copy into new larger memory block using realloc
            d = (Z*)realloc(d, sizeof(Z) * newcapacity);
            memset((void*)&d[capacity], 0, sizeof(Z) * (newcapacity - capacity));
            capacity = newcapacity;
        }
        if (index >= sz) { // at this point capacity>index
            sz = index + 1;
        }

        return d[index];
    }

    int size() const
    {
        return sz;
    }

    /// Resets the array (size == 0).
    void reset()
    {
        sz = 0;
        capacity = 2;
        d = (Z*)realloc(d, sizeof(Z) * capacity);
        memset((void*)d, 0, sizeof(Z) * capacity);
    }

private:
    int sz;
    int capacity;
    Z* d;
};

}
