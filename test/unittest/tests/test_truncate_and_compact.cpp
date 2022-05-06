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

static const std::string testcase_key_base = "key ";
static const std::string testcase_value_base = "a really long string and a value ";

static const std::string testcase_key1 = "key1";
static const std::string testcase_value1 = "value1";

TEST_CASE("Truncate and compact: create simple table", "[compact]")
{
    ConnectionWrapper conn(utils::UnitTestDatabaseHome);
    WT_SESSION_IMPL* sessionImpl = conn.createSession();
    WT_SESSION* session = &(sessionImpl->iface);

    REQUIRE(session->create(session, "table:access", "key_format=S,value_format=S") == 0);

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

int64_t
get_stat(WT_CURSOR *cursor, int stat_field)
{
    cursor->set_key(cursor, stat_field);
    REQUIRE(cursor->search(cursor) == 0);

    const char* desc = nullptr;
    const char* pvalue = nullptr;
    int64_t value = 0;
    REQUIRE(cursor->get_value(cursor, &desc, &pvalue, &value) == 0);
    return value;
}

void dump_stats(WT_SESSION* session) {
    WT_CURSOR* cursor = nullptr;
    REQUIRE(session->open_cursor(session, "statistics:table:access2", nullptr, nullptr, &cursor) == 0);
    // Read the key/value pairs
//    REQUIRE(cursor->reset(cursor) == 0);
//    int ret = cursor->next(cursor);
//    REQUIRE(ret == 0);
//    while (ret == 0) {
//        char const *key = nullptr;
//        uint64_t value = 0;
////        REQUIRE(cursor->get_key(cursor, &key) == 0);
//        const char *desc, *pval;
////        (ret = cursor->get_value(cursor, &desc, &pval, &v))
//        REQUIRE(cursor->get_value(cursor, &desc, &pval, &value) == 0);
//        std::cout << desc << " : " << value << std::endl;
//        ret = cursor->next(cursor);
//    }
//    REQUIRE(ret == WT_NOTFOUND); /* Check for end-of-table. */

    std::cout << "Statistic WT_STAT_DSRC_BTREE_ROW_INTERNAL: "  << get_stat(cursor, WT_STAT_DSRC_BTREE_ROW_INTERNAL)  << std::endl;
    std::cout << "Statistic WT_STAT_DSRC_BTREE_ROW_LEAF: "      << get_stat(cursor, WT_STAT_DSRC_BTREE_ROW_LEAF)      << std::endl;
    std::cout << "Statistic WT_STAT_DSRC_BTREE_MAXIMUM_DEPTH: " << get_stat(cursor, WT_STAT_DSRC_BTREE_MAXIMUM_DEPTH) << std::endl;
//    std::cout << "Statistic WT_STAT_DSRC_BTREE_MAXINTLPAGE: "   << get_stat(cursor, WT_STAT_DSRC_BTREE_MAXINTLPAGE) << std::endl;
//    std::cout << "Statistic WT_STAT_DSRC_BTREE_MAXLEAFKEY: "    << get_stat(cursor, WT_STAT_DSRC_BTREE_MAXLEAFKEY) << std::endl;
//    std::cout << "Statistic WT_STAT_DSRC_BTREE_MAXLEAFPAGE: "   << get_stat(cursor, WT_STAT_DSRC_BTREE_MAXLEAFPAGE) << std::endl;
//    std::cout << "Statistic WT_STAT_DSRC_BTREE_MAXLEAFVALUE: "  << get_stat(cursor, WT_STAT_DSRC_BTREE_MAXLEAFVALUE) << std::endl;
}

uint64_t get_num_key_values(WT_SESSION* session, WT_CURSOR* cursor, uint64_t timeStamp)
{
    // Read the key/value pairs, at timestamp 0x20 (ie before the truncate)
    REQUIRE(session->begin_transaction(session, nullptr) == 0);
    REQUIRE(session->timestamp_transaction_uint(session, WT_TS_TXN_TYPE_READ, timeStamp) == 0);
    char const *key = nullptr;
    char const *value = nullptr;
    REQUIRE(cursor->reset(cursor) == 0);
    int ret = cursor->next(cursor);
    REQUIRE(ret == 0);
    uint64_t numValues = 0;
    while (ret == 0) {
        REQUIRE(cursor->get_key(cursor, &key) == 0);
        REQUIRE(cursor->get_value(cursor, &value) == 0);
        numValues++;
        //            std::cout << key << " : " << value <<
        //              " (via struct, key : " << (const char*) cursor->key.data <<
        //              ", value : " << (const char*) cursor->value.data << ")" << std::endl;
        ret = cursor->next(cursor);
    }
    REQUIRE(ret == WT_NOTFOUND); // Check for end-of-table.
    REQUIRE(session->commit_transaction(session, nullptr) == 0);
    std::cout << "number of key:value pairs: " << numValues <<
      " at timestamp: 0x" << std::hex << timeStamp << std::dec << std::endl;
    return numValues;
}



TEST_CASE("Truncate and compact: table", "[compact]")
{
    // The goal of this test is to ensure that truncate and compact work together
    //
    // The steps in this test are:
    // 1. Add a large number of key/values to a database with small pages,
    //    so that many subtrees are created.
    // 2. Truncate part of the tree, so that at least one subtree is deleted.
    // 3. Perform a cursor traversal on the tree, at a time prior to the truncate
    // 4. Run a compact operation, while a reader is trying to
    //    read some of the data deleted by the truncate, and ensure that this works.

    ConnectionWrapper conn(utils::UnitTestDatabaseHome);
    WT_SESSION_IMPL* sessionImpl = conn.createSession();
    WT_SESSION* session = &(sessionImpl->iface);
    std::string table_name = "table:access2";
    std::string file_name = "file:access2.wt";

    // maybe these are too small
    std::string config =
      "key_format=S,value_format=S,allocation_size=512b,internal_page_max=512b,leaf_page_max=512b";
    REQUIRE(session->create(session, table_name.c_str(), config.c_str()) == 0);

    //    set oldest and stable timestamps
    REQUIRE(conn.getWtConnection()->set_timestamp(conn.getWtConnection(), "oldest_timestamp=1") == 0);
    REQUIRE(conn.getWtConnection()->set_timestamp(conn.getWtConnection(), "stable_timestamp=1") == 0);

    WT_CURSOR* cursor = nullptr;
    REQUIRE(session->open_cursor(session, table_name.c_str(), nullptr, nullptr, &cursor) == 0);

    {
        // Add some key/value pairs, with timestamp 0x10
        int maxOuter = 100;
        int maxInner = 1000;
        for (int outer = 0; outer < maxOuter; outer++) {
            REQUIRE(session->begin_transaction(session, nullptr) == 0);
            for (int inner = 0; inner < maxInner; inner++) {
                int index = 1000000 + (outer * maxInner) + inner;
                std::basic_string key = testcase_key_base + std::to_string(index);
                std::basic_string value = testcase_value_base + std::to_string(index);
                cursor->set_key(cursor, key.c_str());
                cursor->set_value(cursor, value.c_str());
                REQUIRE(cursor->insert(cursor) == 0);
            }
            std::string transactionConfig = std::string("commit_timestamp=10");
            REQUIRE(session->commit_transaction(session, transactionConfig.data()) == 0); // set ts here.
        }
        //REQUIRE(session->checkpoint(session, nullptr) == 0);
        dump_stats(session);
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
        // Truncate, with timestamp = 0x30
        // Need to trigger fast truncate, which will truncate whole pages at one.
        // Need to fast truncate an internal page as well for this test.
        std::cout << "Truncate" << std::endl;
        REQUIRE(session->begin_transaction(session, nullptr) == 0);

        WT_CURSOR* truncate_start = nullptr;
        REQUIRE(session->open_cursor(session, table_name.c_str(), nullptr, nullptr, &truncate_start) == 0);
        std::string key_start = testcase_key_base + std::to_string(1003000);
        truncate_start->set_key(truncate_start, key_start.c_str());
        REQUIRE(truncate_start->search(truncate_start) == 0);

        WT_CURSOR* truncate_end = nullptr;
        REQUIRE(session->open_cursor(session, table_name.c_str(), nullptr, nullptr, &truncate_end) == 0);
        std::string key_end = testcase_key_base + std::to_string(1089999);
        truncate_end->set_key(truncate_end, key_end.c_str());
        REQUIRE(truncate_end->search(truncate_end) == 0);
        REQUIRE(session->truncate(session, nullptr, truncate_start, truncate_end, nullptr) == 0);

        std::string transactionConfig = std::string("commit_timestamp=30");
        REQUIRE(session->commit_transaction(session, transactionConfig.data()) == 0); // set ts here.
        dump_stats(session);
    }

    {
        // Compact
        std::cout << "Compact (0):" << std::endl;
        REQUIRE(session->compact(session, table_name.c_str(), nullptr) == 0);
        dump_stats(session);
    }

#ifdef HAVE_DIAGNOSTIC
//    {
//        // Analyse the btree
//        REQUIRE(__wt_session_get_dhandle(sessionImpl, file_name.c_str(), nullptr, nullptr, 0) == 0);
//        REQUIRE(sessionImpl->dhandle != nullptr);
//        WT_BTREE* btree = S2BT(sessionImpl);
//        REQUIRE(btree != nullptr);
//        WT_REF* ref = &btree->root;
//        REQUIRE(ref != nullptr);
//        REQUIRE(__wt_debug_tree_all(sessionImpl, nullptr, ref, nullptr) == 0);
//    }
#endif

    {
        std::cout << "Checkpoint (1):" << std::endl;
        REQUIRE(session->checkpoint(session, nullptr) == 0);
        dump_stats(session);
        // Compact
        std::cout << "Compact (1):" << std::endl;
        REQUIRE(session->compact(session, table_name.c_str(), nullptr) == 0);
        dump_stats(session);
    }
    {
        // Read the key/value pairs, at timestamp 0x20 (ie before the truncate)
        REQUIRE(get_num_key_values(session, cursor, 0x20) == 100000);
//        REQUIRE(session->begin_transaction(session, nullptr) == 0);
//        REQUIRE(session->timestamp_transaction_uint(session, WT_TS_TXN_TYPE_READ, 0x20) == 0);
//        char const *key = nullptr;
//        char const *value = nullptr;
//        REQUIRE(cursor->reset(cursor) == 0);
//        int ret = cursor->next(cursor);
//        REQUIRE(ret == 0);
//        int numValues = 0;
//        while (ret == 0) {
//            REQUIRE(cursor->get_key(cursor, &key) == 0);
//            REQUIRE(cursor->get_value(cursor, &value) == 0);
//            numValues++;
////            std::cout << key << " : " << value <<
////              " (via struct, key : " << (const char*) cursor->key.data <<
////              ", value : " << (const char*) cursor->value.data << ")" << std::endl;
//            ret = cursor->next(cursor);
//        }
//        REQUIRE(ret == WT_NOTFOUND); // Check for end-of-table.
//        REQUIRE(session->commit_transaction(session, nullptr) == 0);
//        std::cout << "number of key:value pairs: " << numValues << std::endl;
//        REQUIRE(numValues == 100000); // We should see the number of values from prior to the truncate
    }

    //    set oldest and stable timestamps
    REQUIRE(conn.getWtConnection()->set_timestamp(conn.getWtConnection(), "stable_timestamp=35") == 0);
    REQUIRE(conn.getWtConnection()->set_timestamp(conn.getWtConnection(), "oldest_timestamp=35") == 0);

    {
        // Compact
        std::cout << "Compact (2):" << std::endl;
        REQUIRE(session->compact(session, table_name.c_str(), nullptr) == 0);
        dump_stats(session);
        std::cout << "Checkpoint (2):" << std::endl;
        REQUIRE(session->checkpoint(session, nullptr) == 0);
        dump_stats(session);
    }

    // Read the key/value pairs, at timestamp 0x40 (ie after everything)
    REQUIRE(get_num_key_values(session, cursor, 0x40) == 13000);

    // TODO: We get a "scratch buffer allocated and never discarded" warning.
    //       It seems to come from __wt_debug_tree_all.
}
