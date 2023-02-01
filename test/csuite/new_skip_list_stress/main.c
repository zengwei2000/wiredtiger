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
 * This program tests skip list ordering under concurrent workloads. It copies some of the skip list
 * code from the btree, but links against the WiredTiger library for all of the support functions.
 *
 * This is a quick and dirty test for WT-10461. If we ever decide to make this a standard part of
 * the csuite, we'll need to refactor things so it uses the same code as WT, rather than a copy of
 * the code.
 */

// TODO
// - Hit the gd bug
// - confirm the copied WT functions actually match WT
// - Update comments
// - expose insert_search/insert to this test? Probs don't need to copy past the functions
// - How to encourage out of order loads??

#include "test_util.h"
#include <math.h>

extern int __wt_optind;
extern char *__wt_optarg;

static uint64_t seed = 0;

static void usage(void) WT_GCC_FUNC_DECL_ATTRIBUTE((noreturn));

const char *uri = "table:foo";

static volatile bool inserts_running;
static volatile int active_check_threads;

#define NUM_CHECK_THREADS 5

/*
 * usage --
 *     Print a usage message.
 */
static void
usage(void)
{
    fprintf(
      stderr, "usage: %s [-adr] [-h dir] [-S seed] [-t insert threads]\n", progname);
    fprintf(stderr, "Only one of the -adr options may be used\n");
    exit(EXIT_FAILURE);
}


/*
 * search_insert --
 *     Find the location for an insert into the skip list. Based o __wt_search_insert()
 */
static int
search_insert(
  WT_SESSION_IMPL *session, WT_CURSOR_BTREE *cbt, WT_INSERT_HEAD *ins_head, WT_ITEM *srch_key)
{
    WT_INSERT *ins, **insp, *last_ins;
    WT_ITEM key;
    size_t match, skiphigh, skiplow;
    int cmp, i;

    cmp = 0; /* -Wuninitialized */

    /*
     * The insert list is a skip list: start at the highest skip level, then go as far as possible
     * at each level before stepping down to the next.
     */
    match = skiphigh = skiplow = 0;
    ins = last_ins = NULL;
    for (i = WT_SKIP_MAXDEPTH - 1, insp = &ins_head->head[i]; i >= 0;) {
        if ((ins = *insp) == NULL) {
        // This should fix the bug once it fires:
        // WT_ORDERED_READ(ins, *insp);
        // if (ins == NULL) {
            cbt->next_stack[i] = NULL;
            cbt->ins_stack[i--] = insp--;
            continue;
        }

        /*
         * Comparisons may be repeated as we drop down skiplist levels; don't repeat comparisons,
         * they might be expensive.
         */
        if (ins != last_ins) {
            key.data = WT_INSERT_KEY(ins);
            key.size = WT_INSERT_KEY_SIZE(ins);
            match = WT_MIN(skiplow, skiphigh);

            WT_RET(__wt_compare_skip(session, NULL, srch_key, &key, &cmp, &match));
            last_ins = ins;
        }

        if (cmp > 0) { /* Keep going at this level */
            insp = &ins->next[i];
            cbt->next_stack[i] = ins;
            WT_ASSERT(session, match >= skiplow);
            skiplow = match;
        } else if (cmp < 0) { /* Drop down a level */
            cbt->next_stack[i] = ins;
            cbt->ins_stack[i--] = insp--;
            WT_ASSERT(session, match >= skiphigh);
            skiphigh = match;
        } else
            for (; i >= 0; i--) {
                cbt->next_stack[i] = ins->next[i];
                cbt->ins_stack[i] = &ins->next[i];
            }
    }

    // At the end of the search check that all the pointers inserted into 
    // cbt->next_stack are in decreasing order.
    {
        WT_ITEM upper_key, lower_key;

        for(i = WT_SKIP_MAXDEPTH - 2; i >= 0; i--) {
            // Skip if either pointer is to the end of the skiplist, or if both pointers the same
            if(cbt->next_stack[i] == NULL || cbt->next_stack[i+1] == NULL 
                || (cbt->next_stack[i] == cbt->next_stack[i+1]))
                continue;
            lower_key.data = WT_INSERT_KEY(cbt->next_stack[i]);
            lower_key.size = WT_INSERT_KEY_SIZE(cbt->next_stack[i]);

            upper_key.data = WT_INSERT_KEY(cbt->next_stack[i+1]);
            upper_key.size = WT_INSERT_KEY_SIZE(cbt->next_stack[i+1]);

            // force match to zero for a full comparison of keys
            match = 0;
            WT_RET(__wt_compare_skip(session, NULL, &upper_key, &lower_key, &cmp, &match));

            WT_ASSERT((WT_SESSION_IMPL*)session, cmp >= 0);
        }
    }

    /*
     * For every insert element we review, we're getting closer to a better choice; update the
     * compare field to its new value. If we went past the last item in the list, return the last
     * one: that is used to decide whether we are positioned in a skiplist.
     */
    cbt->compare = -cmp;
    cbt->ins = (ins != NULL) ? ins : last_ins;
    cbt->ins_head = ins_head;
    return (0);
}

/*
 * thread_check_run --
 *     A check thread sits in a loop running search_insert for KEY_SIZE - 1 zeroes
 *     Note that this never inserts the key, just searches for it. If there's 
 *     an out of order read we'll catch it in the match >= skiphigh assert in search_insert.
 */
static WT_THREAD_RET
thread_check_run(void *arg)
{
    WT_CONNECTION *conn;
    WT_SESSION *session;
    WT_ITEM check_key;
    WT_CURSOR_BTREE *cbt;
    WT_CURSOR *cursor;

    conn = (WT_CONNECTION*)arg;

    testutil_check(conn->open_session(conn, NULL, NULL, &session));
    testutil_check(session->open_cursor(session, uri, NULL, NULL, &cursor));

    cbt = (WT_CURSOR_BTREE*)cursor;
    cursor->set_key(cursor, "0");
    testutil_check(cursor->search(cursor));
    
    // Set up our search key. It'll always be just after LEFT_BOOKEND in the skip list
    check_key.data = dmalloc(3);
    sprintf((char*)check_key.data, "00");
    /* Include the terminal nul character in the key for easier printing. */
    check_key.size = 3;

    __atomic_fetch_add(&active_check_threads, 1, __ATOMIC_SEQ_CST);

    /* Keep checking the skip list until the insert load has finished */
    while (inserts_running == true)
        WT_IGNORE_RET(search_insert((WT_SESSION_IMPL *)session, cbt, cbt->ins_head, &check_key));

    session->close(session, "");

    return (WT_THREAD_RET_VALUE);
}


static void insert_key(WT_CURSOR *cursor, const char *key) {
    cursor->set_key(cursor, key);
    cursor->set_value(cursor, "");
    testutil_check(cursor->insert(cursor));
}

static int run(const char *working_dir) 
{
    char command[1024], home[1024];
    int status;
    WT_CONNECTION *conn;
    WT_SESSION *session;
    WT_CURSOR *cursor;

    char *new_key;

    wt_thread_t *thr;

    inserts_running = true;
    active_check_threads = 0;

    new_key = dmalloc(10);

    testutil_work_dir_from_path(home, sizeof(home), working_dir);
    testutil_check(__wt_snprintf(command, sizeof(command), "rm -rf %s; mkdir %s", home, home));
    if ((status = system(command)) < 0)
        testutil_die(status, "system: %s", command);

    testutil_check(wiredtiger_open(home, NULL, "create", &conn));
    testutil_check(conn->open_session(conn, NULL, NULL, &session));
    testutil_check(session->create(session, uri, "key_format=S,value_format=S,memory_page_max=1TB")); // !!!!!!!!!
    testutil_check(session->open_cursor(session, uri, NULL, NULL, &cursor));

    insert_key(cursor, "0");
    insert_key(cursor, "9999999999");

    // TODO - static array
    thr = dmalloc(NUM_CHECK_THREADS * sizeof(wt_thread_t));

    for(int i = 0; i < NUM_CHECK_THREADS; i++)
        testutil_check(__wt_thread_create(NULL, &thr[i], thread_check_run, conn));

    testutil_check(session->begin_transaction(session, NULL));

    while (active_check_threads != NUM_CHECK_THREADS)
        ;

    // Run inserts
    for(uint32_t i = 15000; i > 0; i--) {
        sprintf(new_key, "%0*u", 9, i);
        insert_key(cursor, new_key);
    }

    testutil_check(session->commit_transaction(session, NULL));

    inserts_running = false;

    for (int i = 0; i < NUM_CHECK_THREADS; i++)
        testutil_check(__wt_thread_join(NULL, &thr[i]));

    testutil_check(conn->close(conn, ""));

    testutil_clean_test_artifacts(home);
    testutil_clean_work_dir(home);

    return (EXIT_SUCCESS);
}

/*
 * main --
 *     Test body
 */
int
main(int argc, char *argv[])
{
    WT_RAND_STATE rnd;
    const char *working_dir;
    int ch;

    working_dir = "WT_TEST.skip_list_stress";

    while ((ch = __wt_getopt(progname, argc, argv, "h:S:")) != EOF)
        switch (ch) {
        case 'h':
            working_dir = __wt_optarg;
            break;
        case 'S':
            seed = (uint64_t)atoll(__wt_optarg);
            break;
        default:
            usage();
        }
    argc -= __wt_optind;
    if (argc != 0)
        usage();

    if (seed == 0) {
        __wt_random_init_seed(NULL, &rnd);
        seed = rnd.v;
    } else
        rnd.v = seed;

    // Should run for 1 hour on ARM machines
    for(int j = 0; j < 30000; j++) {
        printf("loop %d\n", j);
        run(working_dir);
        // Cause evergreen buffers output and I'm impatient
        fflush(stdout);
    }
    return 0;
}
