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
static const std::string testcase_value_base = "a really long string and a value ";

static const std::string testcase_key1 = "key1";
static const std::string testcase_value1 = "value1";

TEST_CASE("Truncate and compact: create simple table", "[compact]")
{
    ConnectionWrapper conn(utils::UnitTestDatabaseHome);
    WT_SESSION_IMPL* sessionImpl = conn.createSession();
    WT_SESSION* session = &(sessionImpl->iface);

    REQUIRE(session->create(session, "table:access1", "key_format=S,value_format=S") == 0);

    WT_CURSOR* cursor = nullptr;
    REQUIRE(session->open_cursor(session, "table:access", nullptr, nullptr, &cursor) == 0);

    cursor->set_key(cursor, testcase_key1.c_str());
    cursor->set_value(cursor, testcase_value1.c_str());
    REQUIRE(cursor->insert(cursor) == 0);

    char const* key = nullptr;
    char const* value = nullptr;
    REQUIRE(cursor->reset(cursor) == 0);
    int ret = cursor->next(cursor);
    REQUIRE(ret == 0);
    while (ret == 0) {
        REQUIRE(cursor->get_key(cursor, &key) == 0);
        REQUIRE(cursor->get_value(cursor, &value) == 0);
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
    std::string table_name = "table:access2";
    std::string file_name = "file:access2.wt";

    std::string config =
      "key_format=S,value_format=S,allocation_size=512b,internal_page_max=512b,leaf_page_max=512b";
    REQUIRE(session->create(session, table_name.c_str(), config.c_str()) == 0);

    WT_CURSOR* cursor = nullptr;
    REQUIRE(session->open_cursor(session, table_name.c_str(), nullptr, nullptr, &cursor) == 0);

    {
        // Add some key/value pairs
        for (int i = 0; i < 10000; i++) {
            std::basic_string key = testcase_key_base + std::to_string(i);
            std::basic_string value = testcase_value_base + std::to_string(i);
            cursor->set_key(cursor, key.c_str());
            cursor->set_value(cursor, value.c_str());
            REQUIRE(cursor->insert(cursor) == 0);
        }
    }

//    {
//        // Read the key/value pairs
//        char const *key = nullptr;
//        char const *value = nullptr;
//        REQUIRE(cursor->reset(cursor) == 0);
//        int ret = cursor->next(cursor);
//        REQUIRE(ret == 0);
//        while (ret == 0) {
//            REQUIRE(cursor->get_key(cursor, &key) == 0);
//            REQUIRE(cursor->get_value(cursor, &value) == 0);
//            std::cout << key << " : " << value <<
//              " (via struct, key : " << (const char*) cursor->key.data <<
//              ", value : " << (const char*) cursor->value.data << ")" << std::endl;
//            ret = cursor->next(cursor);
//        }
//        REQUIRE(ret == WT_NOTFOUND); /* Check for end-of-table. */
//    }

    {
        // Truncate
        WT_CURSOR* truncate_start = nullptr;
        REQUIRE(session->open_cursor(session, table_name.c_str(), nullptr, nullptr, &truncate_start) == 0);
        std::string key_start = testcase_key_base + std::to_string(3000);
        truncate_start->set_key(truncate_start, key_start.c_str());
        REQUIRE(truncate_start->search(truncate_start) == 0);

        WT_CURSOR* truncate_end = nullptr;
        REQUIRE(session->open_cursor(session, table_name.c_str(), nullptr, nullptr, &truncate_end) == 0);
        std::string key_end = testcase_key_base + std::to_string(9000);
        truncate_end->set_key(truncate_end, key_end.c_str());
        REQUIRE(truncate_end->search(truncate_end) == 0);
        REQUIRE(session->truncate(session, nullptr, truncate_start, truncate_end, nullptr) == 0);
    }

#ifdef HAVE_DIAGNOSTIC
    {
        REQUIRE(__wt_session_get_dhandle(sessionImpl, file_name.c_str(), nullptr, nullptr, 0) == 0);
        REQUIRE(sessionImpl->dhandle != nullptr);
        WT_BTREE* btree = S2BT(sessionImpl);
        REQUIRE(btree != nullptr);
        REQUIRE(S2BT_SAFE(sessionImpl) != nullptr);
        WT_REF* ref = &btree->root;
        REQUIRE(ref != nullptr);
        REQUIRE(__wt_debug_tree_all(sessionImpl, nullptr, ref, nullptr) == 0);
    }
#endif

//    {
//        // Walk the Dhandles in the session impl
//        __wt_data_handle_cache* item = nullptr;
//        TAILQ_FOREACH(item, &sessionImpl->dhandles, q) {
//            std::cout << "__wt_data_handle_cache* item : " << item << std::endl;
//            auto* btree = static_cast<WT_BTREE*>(item->dhandle->handle);
//            std::cout << "  btree : " << btree << std::endl;
//            if (btree) {
//                WT_DATA_HANDLE* dhandle = btree->dhandle;
//                std::cout << "    dhandle : " << dhandle << std::endl;
//                WT_REF& root = btree->root;
//                WT_PAGE* page = root.page;
//                WT_ROW* row = page->u.row;
//                void* key_copy = WT_ROW_KEY_COPY(row);
//                std::cout << "    key_copy : " << key_copy << std::endl;
//                void* ikey = root.ref_ikey;
//                std::cout << "    ikey : " << ikey << std::endl;
//                WT_CELL* cell = nullptr;
//                __wt_row_leaf_key_info(page, key_copy, nullptr, &cell, nullptr, nullptr, nullptr);
//                std::cout << "    cell : " << cell << std::endl;
////#ifdef HAVE_DIAGNOSTIC
////                REQUIRE(btree != nullptr);
////                REQUIRE(__wt_debug_page(session, btree, &btree->root, nullptr) == 0);
////#endif
//            }
//        }
//    }


}
