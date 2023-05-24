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

#include "src/common/random_generator.h"

namespace test_harness {
/* Defines what data is written to the tracking table for use in custom validation. */
class operation_tracker_wt_11062 : public operation_tracker {

public:
    operation_tracker_wt_11062(
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

const std::string KEY_FOO = "336";
const std::string KEY_FOO_SUB6 = "330";

/*
 * Class that defines operations that do nothing as an example. This shows how database operations
 * can be overridden and customized.
 */
class wt_11062 : public test {
public:
    wt_11062(const test_args &args) : test(args)
    {
        init_operation_tracker(
          new operation_tracker_wt_11062(_config->get_subconfig(OPERATION_TRACKER),
            _config->get_bool(COMPRESSION_ENABLED), *_timestamp_manager));
    }

    void
    run() override final
    {
        /* You can remove the call to the base class to fully customize your test. */
        test::run();
    }

    void
    read_operation(thread_worker *tc) override final
    {
        collection &coll = tc->db.get_collection(0);
        scoped_cursor cursor = tc->session.open_scoped_cursor(coll.name);

        logger::log_msg(
          LOG_INFO, type_string(tc->type) + " thread {" + std::to_string(tc->id) + "} commencing.");

        sleep(1);
        while (tc->running()) {
            // position cursor on the key/page we're dirtying in a loop
            cursor->set_key(cursor.get(), KEY_FOO_SUB6);
            cursor->search(cursor.get());
            // Move forward 6. This'll move us onto the page we're modifying and reconciling
            for(int i = 0; i < 6; i++) {
                testutil_check(cursor->next(cursor.get()));
            }
            cursor->reset(cursor.get());
            usleep(100);
        }
    }

    void
    custom_operation(thread_worker *tc) override final
    {
        logger::log_msg(
          LOG_INFO, type_string(tc->type) + " thread {" + std::to_string(tc->id) + "} commencing.");

        // Only one connection
        collection &coll = tc->db.get_collection(0);
        scoped_cursor cursor = tc->session.open_scoped_cursor(coll.name);
        scoped_cursor evict_cursor = tc->session.open_scoped_cursor(coll.name.c_str(), "debug=(release_evict=true)");

        while (tc->running()) {
            // Update key
            cursor->set_key(cursor.get(), KEY_FOO);
            cursor->set_value(cursor.get(), random_generator::instance().generate_pseudo_random_string(tc->value_size));
            cursor->insert(cursor.get());
            cursor->reset(cursor.get());

            // reconcile with a checkpoint
            // logger::log_msg(LOG_ERROR, "ckpt");
            testutil_check(tc->session->checkpoint(tc->session.get(), nullptr));

            // evict
            // logger::log_msg(LOG_ERROR, "evict");
            evict_cursor->set_key(cursor.get(), KEY_FOO);
            evict_cursor->search(cursor.get());
            evict_cursor->reset(cursor.get());
        }

    }
};

} // namespace test_harness
