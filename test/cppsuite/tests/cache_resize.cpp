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

#include "test_harness/test.h"

namespace test_harness {
/* Defines what data is written to the tracking table for use in custom validation. */
class tracking_table_cache_resize : public test_harness::workload_tracking {

    public:
    tracking_table_cache_resize(
      configuration *config, const bool use_compression, timestamp_manager &tsm)
        : workload_tracking(config, use_compression, tsm)
    {
    }

    void
    set_tracking_cursor(scoped_session &tc_session, const tracking_operation &operation,
      const uint64_t &collection_id, const std::string &key, const std::string &value,
      wt_timestamp_t ts, scoped_cursor &op_track_cursor) override final
    {
        uint64_t cache_size =
          ((WT_CONNECTION_IMPL *)connection_manager::instance().get_connection())->cache_size;
        uint64_t txn_id = ((WT_SESSION_IMPL *)tc_session.get())->txn->id;
        op_track_cursor->set_key(op_track_cursor.get(), collection_id, key.c_str(), ts);
        op_track_cursor->set_value(op_track_cursor.get(), cache_size, txn_id);
    }
};

/*
 * This test continuously writes 2MB transactions into the database, while switching the
 * connection cache size between 1MB and 500MB. When transactions are larger than the cache size
 * they are rejected, so only transactions made when cache size if 500MB will be allowed.
 */
class cache_resize : public test_harness::test {
    public:
    cache_resize(const test_harness::test_args &args) : test(args)
    {
        delete this->_workload_tracking;
        this->_workload_tracking =
          new tracking_table_cache_resize(_config->get_subconfig(WORKLOAD_TRACKING),
            _config->get_bool(COMPRESSION_ENABLED), *_timestamp_manager);
    }

    void
    custom_operation(test_harness::thread_context *tc) override final
    {
        bool increase_cache = true;
        const std::string small_cache_size("cache_size=1MB");
        const std::string big_cache_size("cache_size=500MB");

        uint64_t prev_cache_size;
        while (tc->running()) {
            tc->sleep();
            WT_CONNECTION *conn = connection_manager::instance().get_connection();
            prev_cache_size = ((WT_CONNECTION_IMPL *)conn)->cache_size;
            testutil_check(conn->reconfigure(
              conn, increase_cache ? big_cache_size.c_str() : small_cache_size.c_str()));
            increase_cache = !increase_cache;
            logger::log_msg(LOG_INFO,
              "The cache size was updated from " + std::to_string(prev_cache_size) + " to " +
                std::to_string(((WT_CONNECTION_IMPL *)conn)->cache_size));
        }
    }

    void
    insert_operation(test_harness::thread_context *tc) override final
    {
        uint64_t coll_id = 0;
        collection &coll = tc->db.get_collection(coll_id);
        scoped_cursor cursor = tc->session.open_scoped_cursor(coll.name);

        while (tc->running()) {

            tc->transaction.try_begin();

            /* Insert a random value. */
            const std::string value =
              random_generator::instance().generate_pseudo_random_string(tc->value_size);
            bool ret = tc->insert(cursor, coll.id, value);

            if (!ret) {
                tc->transaction.rollback();
            } else if (tc->transaction.can_commit()) {
                /*
                 * The transaction can fit in the current cache size and is ready to be committed.
                 * This means the tracking table will contain a new record to represent this
                 * transaction which will be used during the validation stage.
                 */
                testutil_assert(tc->transaction.commit());
            }

            tc->sleep();
        }

        /* Make sure the last transaction is rolled back now the work is finished. */
        if (tc->transaction.active())
            tc->transaction.rollback();
    }

    void
    validate(const std::string &operation_table_name, const std::string &,
      const std::vector<uint64_t> &) override final
    {
        WT_DECL_RET;
        uint64_t tracked_cache_size, tracked_txn_id;

        scoped_session session = connection_manager::instance().create_session();
        scoped_cursor cursor = session.open_scoped_cursor(operation_table_name);

        uint64_t num_records = 0;
        while ((ret = cursor->next(cursor.get())) == 0) {
            testutil_check(cursor->get_value(cursor.get(), &tracked_cache_size, &tracked_txn_id));
            /*
             * Only transactions that were executed under a large enough cache should have been
             * recorded.
             */
            testutil_assert(tracked_cache_size >= 524288000);
            ++num_records;
        }
        testutil_assert(num_records > 0);
    }
};

} // namespace test_harness
