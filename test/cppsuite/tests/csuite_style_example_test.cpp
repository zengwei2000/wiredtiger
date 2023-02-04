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

/*
 * This file provides an example of how to create a test in C++ using a few features from the
 * framework if any. This file can be used as a template for quick testing and/or when stress
 * testing is not required. For any stress testing, it is encouraged to use the framework, see
 * test_template.cpp and create_script.sh.
 */

#include "src/common/constants.h"
#include "src/common/logger.h"
#include "src/common/random_generator.h"
#include "src/common/thread_manager.h"
#include "src/storage/connection_manager.h"

#include <iostream>

extern "C" {
#include "wiredtiger.h"
#include "test_util.h"
}

using namespace test_harness;

/* Declarations to avoid the error raised by -Werror=missing-prototypes. */
void checkpoint_op(WT_SESSION *session, const std::string &cfg);

bool do_inserts = false;
bool do_reads = false;

void
checkpoint_op(WT_SESSION *session, const std::string &cfg)
{
    logger::log_msg(LOG_INFO, "called checkpoint_op");

    // Sleep for some time and perform a checkpoint.
    logger::log_msg(LOG_INFO, "Sleeping 5s...");
    std::this_thread::sleep_for(std::chrono::seconds(5));
    logger::log_msg(LOG_INFO, "Checkpointing...");
    testutil_check(session->checkpoint(session, cfg.c_str()));
    logger::log_msg(LOG_INFO, "Checkpointing done!");
}

int
main(int argc, char *argv[])
{
    /* Set the program name for error messages. */
    const std::string progname = testutil_set_progname(argv);

    /* Set the tracing level for the logger component. */
    logger::trace_level = LOG_INFO;

    /* Printing some messages. */
    logger::log_msg(LOG_INFO, "Starting " + progname);

    /* Create a connection, set the cache size and specify the home directory. */
    const std::string conn_config = CONNECTION_CREATE + ",cache_size=500MB,checkpoint_sync=false";
    const std::string home_dir = std::string(DEFAULT_DIR) + '_' + progname;

    /* Create connection. */
    connection_manager::instance().create(conn_config, home_dir);
    WT_CONNECTION *conn = connection_manager::instance().get_connection();

    /* Open different sessions. */
    WT_SESSION *session, *checkpoint_session;
    testutil_check(conn->open_session(conn, nullptr, nullptr, &session));
    testutil_check(conn->open_session(conn, nullptr, nullptr, &checkpoint_session));

    // Create a thread to do a checkpoint operation.
    thread_manager t;
    // t.add_thread(checkpoint_op, checkpoint_session, "");

    /* Create an empty collection. */
    const std::string collection_name = "table:my_collection";
    testutil_check(
      session->create(session, collection_name.c_str(), DEFAULT_FRAMEWORK_SCHEMA.c_str()));

    /* V2 */
    WT_CURSOR *checkpoint_cursor;

    // Open a checkpoint cursor, this should not work as no checkpoint exists so far.
    // testutil_assert(session->open_cursor(session, collection_name.c_str(), nullptr,
    // "checkpoint=WiredTigerCheckpoint", &checkpoint_cursor) == ENOENT);

    // Call checkpoint.
    testutil_check(session->checkpoint(session, ""));
    // Call named checkpoint - it should have the same info as the system wide checkpoint.
    testutil_check(session->checkpoint(session, "name=toto"));

    // Get the list of checkpoints here.
    int ret;
    WT_CKPT *ckpt, *ckptbase;
    const std::string filename("file:my_collection.wt");
    std::cout << "Opening checkpoints for " << filename << "..." << std::endl;
    if ((ret = __wt_metadata_get_ckptlist((WT_SESSION *)session, filename.c_str(), &ckptbase)) ==
      0) {
        WT_CKPT_FOREACH (ckptbase, ckpt)
            printf("Checkpoint is %s\n", ckpt->name);
    } else {
        printf("Could not open the metadata %d!\n", ret);
    }

    WT_CKPT ckpt2;
    testutil_check(__wt_meta_checkpoint((WT_SESSION_IMPL *)session, filename.c_str(), nullptr, &ckpt2));
    std::cout << "__wt_meta_checkpoint: " << ckpt2.name << std::endl;

    // Open a checkpoint cursor now.
    // testutil_check(session->open_cursor(session, collection_name.c_str(), nullptr,
    // "checkpoint=WiredTigerCheckpoint", &checkpoint_cursor)); Print the checkpoint id. std::cout
    // << checkpoint_cursor->checkpoint_id(checkpoint_cursor) << std::endl;
    // testutil_check(checkpoint_cursor->close(checkpoint_cursor));

    // Create unamed and named checkpoints.
    // testutil_check(session->checkpoint(session, ""));
    // testutil_check(session->checkpoint(session, "name=toto"));

    // Open a bulk cursor and close it.
    std::cout << "Opening bulk cursor..." << std::endl;
    WT_CURSOR *bulk_cursor;
    testutil_check(
      session->open_cursor(session, collection_name.c_str(), nullptr, "bulk", &bulk_cursor));
    // Insert N records.
    bulk_cursor->set_key(bulk_cursor, "A");
    bulk_cursor->set_value(bulk_cursor, "B");
    bulk_cursor->insert(bulk_cursor);
    testutil_check(bulk_cursor->close(bulk_cursor));
    std::cout << "Bulk cursor closed!" << std::endl;

    std::cout << "Opening checkpoints for " << filename << "..." << std::endl;
    if ((ret = __wt_metadata_get_ckptlist((WT_SESSION *)session, filename.c_str(), &ckptbase)) ==
      0) {
        WT_CKPT_FOREACH (ckptbase, ckpt)
            printf("Checkpoint is %s\n", ckpt->name);
    } else {
        printf("Could not open the metadata %d!\n", ret);
    }

      WT_CKPT ckpt3;
    testutil_check(__wt_meta_checkpoint((WT_SESSION_IMPL *)session, filename.c_str(), nullptr, &ckpt3));
    std::cout << "Retrieved checkpoint " << ckpt3.name << std::endl;

    // Opening a checkpoint cursor should hang.
    uint64_t walltime;
    walltime = 0;
    testutil_check(__wt_meta_read_checkpoint_snapshot(
      (WT_SESSION_IMPL *)session, NULL, NULL, NULL, NULL, NULL, NULL, &walltime));
    std::cout << "Retrieved walltime is " << walltime << std::endl;

    // testutil_check(session->checkpoint(session, ""));
    // const std::string
    // cfg("checkpoint=WiredTigerCheckpoint,debug=(checkpoint_read_timestamp=-1)");
    const std::string cfg("checkpoint=WiredTigerCheckpoint");
    // const std::string cfg("checkpoint=toto");
    std::cout << "Opening checkpoint cursor" << std::endl;
    testutil_check(session->open_cursor(
      session, collection_name.c_str(), nullptr, cfg.c_str(), &checkpoint_cursor));

    // // If we remove the condition.. what do we have?
    // std::cout << checkpoint_cursor->checkpoint_id(checkpoint_cursor) << std::endl;
    // testutil_check(checkpoint_cursor->close(checkpoint_cursor));

    std::cout << "Finish" << std::endl;
    return 0;
    /* */







    // Create a checkpoint here.
    logger::log_msg(LOG_INFO, "Checkpoint...");
    testutil_check(session->checkpoint(session, ""));
    logger::log_msg(LOG_INFO, "Checkpoint done!");

    // logger::log_msg(LOG_INFO, "Checkpoint...");
    // testutil_check(session->checkpoint(session, ""));
    // logger::log_msg(LOG_INFO, "Checkpoint done!");

    // Open a bulk cursor and close it.
    logger::log_msg(LOG_INFO, "Opening bulk cursor...");
    // WT_CURSOR *bulk_cursor;
    testutil_check(
      session->open_cursor(session, collection_name.c_str(), nullptr, "bulk", &bulk_cursor));
    logger::log_msg(LOG_INFO, "Closing bulk cursor...");
    testutil_check(bulk_cursor->close(bulk_cursor));
    logger::log_msg(LOG_INFO, "Closed!");

    // int ret;
    //   do {
    //       // logger::log_msg(LOG_INFO, "Opening checkpoint cursor...");
    //       ret = session->open_cursor(session, collection_name.c_str(), nullptr,
    //         "checkpoint=WiredTigerCheckpoint", &checkpoint_cursor);
    //   } while(ret == EBUSY);

    // testutil_check(session->checkpoint(session, ""));
    // logger::log_msg(LOG_INFO, "Checkpoint done");
    logger::log_msg(LOG_INFO, "Opening checkpoint cursor...");
    testutil_check(session->open_cursor(session, collection_name.c_str(), nullptr,
      "checkpoint=WiredTigerCheckpoint", &checkpoint_cursor));
    std::cout << "In the test, the checkpoint id from the cursor is "
              << checkpoint_cursor->checkpoint_id(checkpoint_cursor) << std::endl;
    testutil_check(checkpoint_cursor->close(checkpoint_cursor));

    logger::log_msg(LOG_INFO, "Joining...");
    t.join();

    /* Another message. */
    logger::log_msg(LOG_INFO, "End of test.");

    return (0);
}
