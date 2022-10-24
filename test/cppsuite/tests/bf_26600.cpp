/*-
 * Public Domain 2014-present MongoDB, Inc.
 * Public Domain 2008-2014 WiredTiger, Inc.
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "src/common/constants.h"
#include "src/common/logger.h"
#include "src/main/test.h"

namespace test_harness {
/* Defines what data is written to the tracking table for use in custom validation. */
class operation_tracker_bf_26600 : public operation_tracker {

    public:
    operation_tracker_bf_26600(
      configuration *config, const bool use_compression, timestamp_manager &tsm)
        : operation_tracker(config, use_compression, tsm)
    {
    }

    void
    set_tracking_cursor(WT_SESSION *session, const tracking_operation &operation,
      const uint64_t &collection_id, const std::string &key, const std::string &value,
      wt_timestamp_t ts, scoped_cursor &op_track_cursor) override final
    {
        /* You can replace this call to define your own tracking table contents. */
        operation_tracker::set_tracking_cursor(
          session, operation, collection_id, key, value, ts, op_track_cursor);
    }
};

/*
 * Class that defines operations that do nothing as an example. This shows how database operations
 * can be overridden and customized.
 */
class bf_26600 : public test {
    public:
    bf_26600(const test_args &args) : test(args)
    {
        init_operation_tracker(
          new operation_tracker_bf_26600(_config->get_subconfig(OPERATION_TRACKER),
            _config->get_bool(COMPRESSION_ENABLED), *_timestamp_manager));
    }


    void
    insert_operation(thread_worker *tc) override final
    {
        logger::log_msg(
        LOG_INFO, type_string(tc->type) + " thread {" + std::to_string(tc->id) + "} commencing.");

        /* Open a cursor on the 1 collection. */
        auto &coll = tc->db.get_collection(0);
        scoped_cursor cursor = tc->session.open_scoped_cursor(coll.name);

        while (tc->running()) {
            tc->txn.begin();
            uint64_t key = random_generator::instance().generate_integer<uint64_t>(0, coll.get_key_count());
            std::string chosen_key = tc->pad_string(std::to_string(key), tc->key_size);
            cursor->set_key(cursor.get(), chosen_key.c_str());
            int ret = cursor->remove(cursor.get());
            bool rollback = false;
            if (ret == WT_ROLLBACK || WT_NOTFOUND) {
                rollback = true;
                ret = 0;
            }
            testutil_assert(ret == 0);

            if (!rollback) {
                tc->txn.commit();
                tc->txn.begin();
                cursor->set_key(cursor.get(), chosen_key.c_str());
                std::string new_value = random_generator::instance().generate_pseudo_random_string(tc->value_size);
                cursor->set_value(cursor.get(), new_value);
                ret = cursor->insert(cursor.get());
                if (ret == WT_ROLLBACK) {
                    rollback = true;
                    ret = 0;
                }
            }

            if (random_generator::instance().generate_integer<uint64_t>(0, 100) > 90)
                rollback = true;
            if (rollback)
                tc->txn.rollback();
            else
                tc->txn.commit();
            tc->sleep();
        }
    }



    /* Walk the full collection using cursor->next() every op_count. */
    void
    read_operation(thread_worker *tc) override final
    {
        logger::log_msg(
            LOG_INFO, type_string(tc->type) + " thread {" + std::to_string(tc->id) + "} commencing.");

        /* Open a cursor on the 1 collection. */
        scoped_cursor cursor = tc->session.open_scoped_cursor(tc->db.get_collection(0).name);
        std::string last_key;
        while (tc->running()) {
            tc->txn.begin();
            while (tc->txn.active() && tc->running()) {
                auto ret = cursor->next(cursor.get());
                tc->txn.add_op();
                /* We don't expect rollback as the cache shouldn't be full. */
                testutil_assert(ret == WT_NOTFOUND || ret == 0);
                if (ret == WT_NOTFOUND) {
                    testutil_check(cursor->reset(cursor.get()));
                    last_key = "";
                    continue;
                }

                char *current_key;
                testutil_check(cursor->get_key(cursor.get(), &current_key));
                std::string current = std::string(current_key);

                if (last_key != "") {
                    if (logger::trace_level == LOG_TRACE)
                    logger::log_msg(
                        LOG_TRACE, type_string(tc->type) + " thread {" + std::to_string(tc->id) + "} saw key {" + last_key + "} then {" + current + "}");
                    testutil_assert(last_key < current);
                }
                last_key = std::move(current);

                if (tc->txn.can_commit())
                    tc->txn.commit();
            }
            tc->sleep();
        }
        /* Make sure the last transaction is rolled back now the work is finished. */
        tc->txn.try_rollback();
    }

    void
    update_operation(thread_worker *tc) override final
    {
        logger::log_msg(
            LOG_INFO, type_string(tc->type) + " thread {" + std::to_string(tc->id) + "} commencing.");

        /* Open a cursor on the 1 collection. */
        auto& coll = tc->db.get_collection(0);
        scoped_cursor cursor = tc->session.open_scoped_cursor(coll.name);
        while (tc->running()) {
            tc->txn.begin();
            /* Choose a record between 0 and key_count. */
            uint64_t key = random_generator::instance().generate_integer<uint64_t>(0, coll.get_key_count());
            std::string chosen_key = tc->pad_string(std::to_string(key), tc->key_size);
            cursor->set_key(cursor.get(), chosen_key.c_str());
            std::string new_value = random_generator::instance().generate_pseudo_random_string(tc->value_size);
            cursor->set_value(cursor.get(), new_value.c_str());
            if (logger::trace_level == LOG_TRACE)
            logger::log_msg(
                LOG_TRACE, type_string(tc->type) + " thread {" + std::to_string(tc->id) + "} updating key {" + chosen_key + "} with value {" + new_value + "}");

            int ret = cursor->update(cursor.get());
            bool rollback = true;
            if (ret != WT_ROLLBACK && random_generator::instance().generate_integer<uint64_t>(0, 100) > 90)
                rollback = false;
            testutil_assert(ret == 0 || ret == WT_ROLLBACK);
            if (rollback)
                tc->txn.rollback();
            else
                tc->txn.commit();
            tc->sleep();
        }
    }
};

} // namespace test_harness
