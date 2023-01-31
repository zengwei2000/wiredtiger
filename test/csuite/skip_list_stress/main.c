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

#define NUM_KEYS (1 << 20)
#define KEY_BIT_SIZE 10

/* Test parameters. Eventually these should become command line args */

// Currently 3 types of threads:
// 1. Check threads continually call search_insert, but never insert
//    If the OOO read happens they'll fail the assert `match >= skiphigh`
// 2. The Insert thread inserts keys in a decreasing order. It chooses keys 
//    that will trigger the assert in the check threads

#define CHECK_THREADS 3  /* Can change this as needed */
#define INSERT_THREAD 1 /* !!!! We only want 1 insert thread. Don't change this !!!! */

typedef struct {
    WT_CONNECTION *conn;
    WT_INSERT_HEAD *ins_head;
    uint32_t id;
} THREAD_DATA;


// Test lifecycle - Thread interactions:
// 1. Begin test
// 2. Start up insert thread. It'll populate the initial skiplist and then wait for check threads
// 3. Once insert thread is ready, check threads start running in a loop and signal insert thread
//        In this case invalidate threads is considered one of the check threads
// 4. Once *all* check threads are running, insert thread starts inserting key
// 5. Once insert thread is finished it signals the checks threads and waits
// 6. The check threads stop
// 7. Once all check threads are stopped, insert thread frees memory and stops
static volatile int active_check_threads;
static volatile int active_insert_threads;

static void usage(void) WT_GCC_FUNC_DECL_ATTRIBUTE((noreturn));

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
 * We don't care about the values we store in our mock insert list. So all entries will point to the
 * dummy update. Likewise, the insert code uses the WT page lock when it needs to exclusive access.
 * We're don't have that, so we just set up a single global spinlock that all threads use since
 * they're all operating on the same skiplist.
 */
static WT_UPDATE dummy_update;

static WT_SPINLOCK page_lock;

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
            last_ins = ins;
            key.data = WT_INSERT_KEY(ins);
            key.size = WT_INSERT_KEY_SIZE(ins);
            match = WT_MIN(skiplow, skiphigh);

            WT_RET(__wt_compare_skip(session, NULL, srch_key, &key, &cmp, &match));
        }

        if (cmp > 0) { /* Keep going at this level */
            insp = &ins->next[i];
            cbt->next_stack[i] = ins;
            skiplow = match;
        } else if (cmp < 0) { /* Drop down a level */
            cbt->next_stack[i] = ins;
            cbt->ins_stack[i--] = insp--;
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
 * insert_simple_func --
 *     Add a WT_INSERT entry to the middle of a skiplist. Copy of __insert_simple_func().
 */
static inline int
insert_simple_func(
  WT_SESSION_IMPL *session, WT_INSERT ***ins_stack, WT_INSERT *new_ins, u_int skipdepth)
{
    u_int i;

    WT_UNUSED(session);

    /*
     * Update the skiplist elements referencing the new WT_INSERT item. If we fail connecting one of
     * the upper levels in the skiplist, return success: the levels we updated are correct and
     * sufficient. Even though we don't get the benefit of the memory we allocated, we can't roll
     * back.
     *
     * All structure setup must be flushed before the structure is entered into the list. We need a
     * write barrier here, our callers depend on it. Don't pass complex arguments to the macro, some
     * implementations read the old value multiple times.
     */
    for (i = 0; i < skipdepth; i++) {
        WT_INSERT *old_ins = *ins_stack[i];
        if (old_ins != new_ins->next[i] || !__wt_atomic_cas_ptr(ins_stack[i], old_ins, new_ins))
            return (i == 0 ? WT_RESTART : 0);
    }

    return (0);
}

/*
 * insert_serial_func --
 *     Add a WT_INSERT entry to a skiplist. Copy of __insert_serial_func()
 */
static inline int
insert_serial_func(WT_SESSION_IMPL *session, WT_INSERT_HEAD *ins_head, WT_INSERT ***ins_stack,
  WT_INSERT *new_ins, u_int skipdepth)
{
    u_int i;

    /* The cursor should be positioned. */
    WT_ASSERT(session, ins_stack[0] != NULL);

    /*
     * Update the skiplist elements referencing the new WT_INSERT item.
     *
     * Confirm we are still in the expected position, and no item has been added where our insert
     * belongs. If we fail connecting one of the upper levels in the skiplist, return success: the
     * levels we updated are correct and sufficient. Even though we don't get the benefit of the
     * memory we allocated, we can't roll back.
     *
     * All structure setup must be flushed before the structure is entered into the list. We need a
     * write barrier here, our callers depend on it. Don't pass complex arguments to the macro, some
     * implementations read the old value multiple times.
     */
    for (i = 0; i < skipdepth; i++) {
        WT_INSERT *old_ins = *ins_stack[i];
        if (old_ins != new_ins->next[i] || !__wt_atomic_cas_ptr(ins_stack[i], old_ins, new_ins))
            return (i == 0 ? WT_RESTART : 0);
        if (ins_head->tail[i] == NULL || ins_stack[i] == &ins_head->tail[i]->next[i])
            ins_head->tail[i] = new_ins;
    }

    return (0);
}

/*
 * insert_serial --
 *     Top level function for inserting a WT_INSERT into a skiplist. Based on __wt_insert_serial()
 */
static inline int
insert_serial(WT_SESSION_IMPL *session, WT_INSERT_HEAD *ins_head, WT_INSERT ***ins_stack,
  WT_INSERT **new_insp, u_int skipdepth)
{
    WT_DECL_RET;
    WT_INSERT *new_ins;
    u_int i;
    bool simple;

    /* Clear references to memory we now own and must free on error. */
    new_ins = *new_insp;
    *new_insp = NULL;

    simple = true;
    for (i = 0; i < skipdepth; i++)
        if (new_ins->next[i] == NULL)
            simple = false;

    if (simple)
        ret = insert_simple_func(session, ins_stack, new_ins, skipdepth);
    else {
        __wt_spin_lock(session, &page_lock);
        ret = insert_serial_func(session, ins_head, ins_stack, new_ins, skipdepth);
        __wt_spin_unlock(session, &page_lock);
    }

    if (ret != 0) {
        /* Free unused memory on error. */
        __wt_free(session, new_ins);
        return (ret);
    }

    return (0);
}

/*
 * row_insert --
 *     Our version of the __wt_row_modify() function, with everything stripped out except for the
 *     relevant insert path.
 */
static int
row_insert(WT_CURSOR_BTREE *cbt, const WT_ITEM *key, WT_INSERT_HEAD *ins_head)
{
    // TODO - Significant deviation from the WT implementation. 
    //        Need to confirm this still hits the correct code paths.
    //        Other functions also deviate but not as much as this one.
    WT_DECL_RET;
    WT_INSERT *ins;
    WT_SESSION_IMPL *session;
    size_t ins_size;
    u_int i, skipdepth;

    ins = NULL;
    session = CUR2S(cbt);

    /*
     * Allocate the insert array as necessary.
     *
     * We allocate an additional insert array slot for insert keys sorting less than any key on the
     * page. The test to select that slot is baroque: if the search returned the first page slot, we
     * didn't end up processing an insert list, and the comparison value indicates the search key
     * was smaller than the returned slot, then we're using the smallest-key insert slot. That's
     * hard, so we set a flag.
     */

    /* Choose a skiplist depth for this insert. */
    skipdepth = __wt_skip_choose_depth(session);

    /*
     * Allocate a WT_INSERT/WT_UPDATE pair and transaction ID, and update the cursor to reference it
     * (the WT_INSERT_HEAD might be allocated, the WT_INSERT was allocated).
     */
    WT_ERR(__wt_row_insert_alloc(session, key, skipdepth, &ins, &ins_size));
    cbt->ins_head = ins_head;
    cbt->ins = ins;

    ins->upd = &dummy_update;
    ins_size += WT_UPDATE_SIZE;

    /*
     * If there was no insert list during the search, the cursor's information cannot be correct,
     * search couldn't have initialized it.
     *
     * Otherwise, point the new WT_INSERT item's skiplist to the next elements in the insert list
     * (which we will check are still valid inside the serialization function).
     *
     * The serial mutex acts as our memory barrier to flush these writes before inserting them into
     * the list.
     */
    if (cbt->ins_stack[0] == NULL)
        for (i = 0; i < skipdepth; i++) {
            cbt->ins_stack[i] = &ins_head->head[i];
            ins->next[i] = cbt->next_stack[i] = NULL;
        }
    else
        for (i = 0; i < skipdepth; i++)
            ins->next[i] = cbt->next_stack[i];

    /* Insert the WT_INSERT structure. */
    WT_ERR(insert_serial(session, cbt->ins_head, cbt->ins_stack, &ins, skipdepth));

err:
    return (ret);
}

/*
 * insert --
 *     Test function that inserts a new entry with the given key string into our skiplist.
 */
static int
insert(WT_SESSION_IMPL *session, WT_CURSOR_BTREE *cbt, WT_INSERT_HEAD *ins_head, char *key)
{
    WT_ITEM new;

    new.data = key;
    /* Include the terminal nul character in the key for easier printing. */
    new.size = strlen(key) + 1;
    WT_RET(search_insert(session, cbt, ins_head, &new));
    WT_RET(row_insert(cbt, &new, ins_head));

    return (0);
}

/*
 * thread_insert_run --
 *     An insert thread. Continually insert keys in decreasing order. 
 *     These keys are intentionally chosen such that each newly inserted 
 *     key has a longer matching prefix with our search key in check_run. 
 */
static WT_THREAD_RET
thread_insert_run(void *arg)
{
    WT_CONNECTION *conn;
    WT_CURSOR_BTREE *cbt;
    WT_INSERT_HEAD *ins_head;
    WT_SESSION *session;
    THREAD_DATA *td;
    uint32_t i;
    char **key_list;
    char *min_key, *max_key;

    td = (THREAD_DATA *)arg;
    conn = td->conn;
    ins_head = td->ins_head;

    testutil_check(conn->open_session(conn, NULL, NULL, &session));

    /* Set up state as if we have a btree that is accessing an insert list. */
    cbt = dcalloc(1, sizeof(WT_CURSOR_BTREE));
    ((WT_CURSOR *)cbt)->session = session;

    // We're assuming only one INSERT THREAD in use here.
    // It'll take responsibility for setting up the initial state

    /*
     * Generate the keys.
     * N.B., the key strings here are stored in the skip list. So we need a separate buffer for each
     * key.
     */
    key_list = dmalloc(NUM_KEYS * sizeof(char*));
    for (i = 0; i < NUM_KEYS; i++) {
        key_list[i] = dmalloc(KEY_BIT_SIZE);
        sprintf(key_list[i], "%0*u", KEY_BIT_SIZE - 1, i);
    }

    min_key = dmalloc(2);
    sprintf(min_key, "0");

    max_key = dmalloc(KEY_BIT_SIZE);
    for(int j = 0; j < KEY_BIT_SIZE - 1; j++)
        max_key[j] = '9';
    max_key[KEY_BIT_SIZE - 1] = '\0';

    WT_IGNORE_RET(insert((WT_SESSION_IMPL *)session, cbt, ins_head, min_key));
    WT_IGNORE_RET(insert((WT_SESSION_IMPL *)session, cbt, ins_head, max_key));

    __atomic_fetch_add(&active_insert_threads, 1, __ATOMIC_SEQ_CST);
    while ( active_check_threads != (CHECK_THREADS))
        ;

    /* Insert the keys in decreasing order. */
    for (i = NUM_KEYS - 1; i > 0; i--) {
        WT_IGNORE_RET(insert((WT_SESSION_IMPL *)session, cbt, ins_head, key_list[i]));
    }

    __atomic_fetch_sub(&active_insert_threads, 1, __ATOMIC_SEQ_CST);

    // Wait till all checks are joined so we don't free the skiplist under them
    while (active_check_threads != 0)
        ;   

    free(cbt);
    for (i = 0; i < NUM_KEYS; i++) {
        free(key_list[i]);
    }
    free(key_list);
    free(min_key);
    free(max_key);

    return (WT_THREAD_RET_VALUE);
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
    WT_INSERT_HEAD *ins_head;
    WT_SESSION *session;
    THREAD_DATA *td;
    WT_ITEM check_key;
    WT_CURSOR_BTREE *cbt;

    td = (THREAD_DATA *)arg;
    conn = td->conn;
    ins_head = td->ins_head;

    testutil_check(conn->open_session(conn, NULL, NULL, &session));

    /* Set up state as if we have a btree that is accessing an insert list. */
    cbt = dcalloc(1, sizeof(WT_CURSOR_BTREE));
    ((WT_CURSOR *)cbt)->session = session;
    
    // Set up our search key. It'll always be just after LEFT_BOOKEND in the skip list
    check_key.data = dmalloc(3);
    sprintf((char*)check_key.data, "00");
    /* Include the terminal nul character in the key for easier printing. */
    check_key.size = 3;

    while(active_insert_threads != INSERT_THREAD)
        ;
    __atomic_fetch_add(&active_check_threads, 1, __ATOMIC_SEQ_CST);

    /* Keep checking the skip list until the insert load has finished */
    while (active_insert_threads != 0)
        WT_IGNORE_RET(search_insert((WT_SESSION_IMPL *)session, cbt, ins_head, &check_key));

    __atomic_fetch_sub(&active_check_threads, 1, __ATOMIC_SEQ_CST);

    return (WT_THREAD_RET_VALUE);
}

static int run(const char *working_dir) 
{
    char command[1024], home[1024];
    int status;
    WT_CONNECTION *conn;
    WT_SESSION *session;
    WT_INSERT_HEAD *ins_head;
    THREAD_DATA *td;
    wt_thread_t *thr;
    uint32_t nthreads, i;
    
    nthreads = CHECK_THREADS + INSERT_THREAD;

    testutil_work_dir_from_path(home, sizeof(home), working_dir);
    testutil_check(__wt_snprintf(command, sizeof(command), "rm -rf %s; mkdir %s", home, home));
    if ((status = system(command)) < 0)
        testutil_die(status, "system: %s", command);

    testutil_check(wiredtiger_open(home, NULL, "create", &conn));
    testutil_check(conn->open_session(conn, NULL, NULL, &session));
    testutil_check(__wt_spin_init((WT_SESSION_IMPL *)session, &page_lock, "fake page lock"));
    ins_head = dcalloc(1, sizeof(WT_INSERT_HEAD));

    /* Set up threads */
    td = dmalloc(nthreads * sizeof(THREAD_DATA));
    for (i = 0; i < nthreads; i++) {
        td[i].conn = conn;
        td[i].id = i;
        td[i].ins_head = ins_head;
    }

    thr = dmalloc(nthreads * sizeof(wt_thread_t));

    /* Start threads */
    active_check_threads = 0;
    active_insert_threads = 0;
    for (i = 0; i < nthreads; i++)
        if (i < CHECK_THREADS)
            testutil_check(__wt_thread_create(NULL, &thr[i], thread_check_run, &td[i]));
        else
            testutil_check(__wt_thread_create(NULL, &thr[i], thread_insert_run, &td[i]));

    /* Wait check threads to stop */
    for (i = 0; i < CHECK_THREADS; i++)
        testutil_check(__wt_thread_join(NULL, &thr[i]));

    /* Wait for insert thread to complete */
    for (i = CHECK_THREADS; i < nthreads; i++)
        testutil_check(__wt_thread_join(NULL, &thr[i]));
    
    testutil_check(conn->close(conn, ""));

    // Inserts get allocated as part of __wt_row_insert_alloc.
    // Walk the skiplist and clean them up at the end of the test.
    {
        WT_INSERT *ins;
        WT_INSERT *prev_ins;
        prev_ins = NULL;
        ins = ins_head->head[0];
        while(ins->next[0] != NULL) {
            if(prev_ins != NULL)
                free(prev_ins);
            prev_ins = ins;
            ins = ins->next[0];
        }
    }

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
    for(int j = 0; j < 600; j++) {
        printf("loop %d\n", j);
        run(working_dir);
        // Cause evergreen buffers output and I'm impatient
        fflush(stdout);
    }
    return 0;
}
