/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
/**
 * This is a modeling file for Coverity Scan. Modeling helps to avoid false positives.
 *
 * - A model file can't import any header files.
 * - Therefore only some built-in primitives like int, char and void are available.
 * - Modeling doesn't need full structs and typedefs. Rudimentary structs and similar types are
 *   sufficient.
 * - An uninitialized local pointer is not an error. It signifies that the variable could be either
 *   NULL or have some data.
 *
 * Coverity Scan doesn't pick up modifications automatically. The model file must be uploaded by an
 * admin in the analysis settings of https://scan.coverity.com/projects/kwinft.
 */

namespace QTest
{

// In tests failing this QTest library function through the QVERIFY macro aborts further execution.
bool qVerify(bool statement,
             char const* /*statementStr*/,
             char const* /*description*/,
             char const* /*file*/,
             int /*line*/)
{
    if (!statement) {
        __coverity_panic__();
    }
}

}
