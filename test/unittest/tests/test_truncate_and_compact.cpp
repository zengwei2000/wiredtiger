/*-
* Copyright (c) 2014-present MongoDB, Inc.
* Copyright (c) 2008-2014 WiredTiger, Inc.
*	All rights reserved.
*
* See the file LICENSE for redistribution information.
*/

#include <catch2/catch.hpp>
#include <iostream>
#include "wt_internal.h"
#include "wiredtiger.h"
#include "utils.h"
#include "wrappers/connection_wrapper.h"
#include "wrappers/tailq_wrapper.h"

static const std::string testcase_key_base = "key ";
static const std::string testcase_value_base = "value ";

static const std::string testcase_key1 = "key1";
static const std::string testcase_value1 = "value1";

TEST_CASE("Truncate and compact: create simple table", "[compact]")
{
    ConnectionWrapper conn(utils::UnitTestDatabaseHome);
    WT_SESSION_IMPL* sessionImpl = conn.createSession();
    WT_SESSION* session = &(sessionImpl->iface);

    REQUIRE(utils::isSuccessResult(session->create(session, "table:access", "key_format=S,value_format=S")));

    WT_CURSOR* cursor = nullptr;
    REQUIRE(utils::isSuccessResult(session->open_cursor(session, "table:access", nullptr, nullptr, &cursor)));

    cursor->set_key(cursor, testcase_key1.c_str());
    cursor->set_value(cursor, testcase_value1.c_str());
    REQUIRE(utils::isSuccessResult(cursor->insert(cursor)));

    char const* key = nullptr;
    char const* value = nullptr;
    REQUIRE(utils::isSuccessResult(cursor->reset(cursor)));
    int ret = cursor->next(cursor);
    REQUIRE(utils::isSuccessResult(ret));
    while (utils::isSuccessResult(ret)) {
        REQUIRE(utils::isSuccessResult(cursor->get_key(cursor, &key)));
        REQUIRE(utils::isSuccessResult(cursor->get_value(cursor, &value)));
        REQUIRE(key == testcase_key1);
        REQUIRE(value == testcase_value1);
        ret = cursor->next(cursor);
    }
    REQUIRE(ret == WT_NOTFOUND); /* Check for end-of-table. */
}


TEST_CASE("Truncate and compact: table", "[compact]")
{
    ConnectionWrapper conn(utils::UnitTestDatabaseHome);
    WT_SESSION_IMPL* sessionImpl = conn.createSession();
    WT_SESSION* session = &(sessionImpl->iface);

    REQUIRE(utils::isSuccessResult(session->create(session, "table:access", "key_format=S,value_format=S")));

    WT_CURSOR* cursor = nullptr;
    REQUIRE(utils::isSuccessResult(session->open_cursor(session, "table:access", nullptr, nullptr, &cursor)));

    {
        // Add some key/value pairs
        for (int i = 0; i < 100; i++) {
            std::basic_string key = testcase_key_base + std::to_string(i);
            std::basic_string value = testcase_value_base + std::to_string(i);
            cursor->set_key(cursor, key.c_str());
            cursor->set_value(cursor, value.c_str());
            REQUIRE(utils::isSuccessResult(cursor->insert(cursor)));
        }
    }

    {
        // Read the key/value pairs
        char const *key = nullptr;
        char const *value = nullptr;
        REQUIRE(utils::isSuccessResult(cursor->reset(cursor)));
        int ret = cursor->next(cursor);
        REQUIRE(utils::isSuccessResult(ret));
        while (utils::isSuccessResult(ret)) {
            REQUIRE(utils::isSuccessResult(cursor->get_key(cursor, &key)));
            REQUIRE(utils::isSuccessResult(cursor->get_value(cursor, &value)));
            //std::cout << key << " : " << value << std::endl;
            ret = cursor->next(cursor);
        }
        REQUIRE(ret == WT_NOTFOUND); /* Check for end-of-table. */
    }

    {
        // Walk the Dhandles in the session impl
        __wt_data_handle_cache* item = nullptr;
        TAILQ_FOREACH(item, &sessionImpl->dhandles, q) {
            std::cout << "__wt_data_handle_cache* item : " << item << std::endl;
            auto* btree = static_cast<WT_BTREE*>(item->dhandle->handle);
            std::cout << "  btree : " << btree << std::endl;
            if (btree) {
                WT_DATA_HANDLE* dhandle = btree->dhandle;
                std::cout << "    dhandle : " << dhandle << std::endl;
                WT_REF& root = btree->root;
                WT_PAGE* page = root.page;
                WT_ROW* row = page->u.row;
                void* key = WT_ROW_KEY_COPY(row);
                std::cout << "    key : " << key << std::endl;
                auto ikey = root.ref_ikey;
                std::cout << "    ikey : " << ikey << std::endl;
            }
        }

    }
}
