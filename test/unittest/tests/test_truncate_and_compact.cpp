/*-
* Copyright (c) 2014-present MongoDB, Inc.
* Copyright (c) 2008-2014 WiredTiger, Inc.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/

#include <catch2/catch.hpp>
#include "wt_internal.h"
#include "wiredtiger.h"
#include "utils.h"
#include "wrappers/connection_wrapper.h"

const std::string key1 = "key1";
const std::string value1 = "value1";

TEST_CASE("Truncate and compact", "[compact]")
{
    ConnectionWrapper conn(utils::UnitTestDatabaseHome);
    WT_SESSION_IMPL* sessionImpl = conn.createSession();
    WT_SESSION* session = &(sessionImpl->iface);

    REQUIRE(utils::isSuccessResult(session->create(session, "table:access", "key_format=S,value_format=S")));

    WT_CURSOR* cursor = nullptr;
    REQUIRE(utils::isSuccessResult(session->open_cursor(session, "table:access", nullptr, nullptr, &cursor)));

    cursor->set_key(cursor, key1.c_str());
    cursor->set_value(cursor, value1.c_str());
    REQUIRE(utils::isSuccessResult(cursor->insert(cursor)));

    char const* key = nullptr;
    char const* value = nullptr;
    REQUIRE(utils::isSuccessResult(cursor->reset(cursor)));
    int ret = 0;
    while (utils::isSuccessResult(ret = cursor->next(cursor))) {
        REQUIRE(utils::isSuccessResult(cursor->get_key(cursor, &key)));
        REQUIRE(utils::isSuccessResult(cursor->get_value(cursor, &value)));
        REQUIRE(key == key1);
        REQUIRE(value == value1);
    }
    REQUIRE(ret == WT_NOTFOUND); /* Check for end-of-table. */
}
