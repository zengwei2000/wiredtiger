#include <catch2/catch.hpp>

#include "wt_internal.h"
#include "wrappers/connection_wrapper.h"
#include "wrappers/cache_wrapper.h"
#include "wrappers/mock_session.h"
#include "utils.h"

//std::shared_ptr<MockSession> session = MockSession::buildTestMockSession();

//    const char *config0 = "create,cache_size=5GB,log=(enabled,recover=on),statistics=(all),"
//      "shared_cache=(chunk=10MB,name=,quota=0,reserve=0,size=500MB),"
//      "cache_overhead=8,cache_size=100MB,cache_max_wait_ms=0,"
//      "eviction=(threads_max=8,threads_min=1),"
//      "eviction_checkpoint_target=1,eviction_dirty_target=5,eviction_dirty_trigger=20,"
//      "eviction_target=80,eviction_trigger=95,eviction_updates_target=0,eviction_updates_trigger=0";
//    const char *config[] = { config0, nullptr };
//    cache_wrapper cache(session_impl, config);


TEST_CASE("Cache: create and cleanup using ConnectionWrapper and WT_CONNECTION.createSession()",
  "cache")
{
    ConnectionWrapper conn(DB_HOME);
    WT_SESSION_IMPL *session_impl = conn.createSession();
    __wt_cache_stats_update(session_impl);
    WT_CACHE* cache = S2C(session_impl)->cache;
    CHECK(cache->eviction_dirty_target == 5);
    CHECK(cache->eviction_dirty_trigger == 20);
    CHECK(cache->eviction_trigger == 95);
    CHECK(cache->eviction_target == 80);
    CHECK(__wt_cache_bytes_inuse(cache) >= 2500);
    CHECK(__wt_cache_bytes_inuse(cache) <= 3000);
    CHECK(cache->overhead_pct == 8);
    //TODO: create a test for __wt_btree_dominating_cache()
}


//TEST_CASE("Cache: create and cleanup using cache_wrapper", "cache")
//{
//
//    std::shared_ptr<MockSession> session = MockSession::buildTestMockSession();
//    WT_SESSION_IMPL *session_impl = session->getWtSessionImpl();
//
//    const char *config0 = "create,cache_size=5GB,log=(enabled,recover=on),statistics=(all),"
//      "shared_cache=(chunk=10MB,name=,quota=0,reserve=0,size=500MB),"
//      "cache_overhead=8,cache_size=100MB,cache_max_wait_ms=0,"
//      "eviction=(threads_max=8,threads_min=1),"
//      "eviction_checkpoint_target=1,eviction_dirty_target=5,eviction_dirty_trigger=20,"
//      "eviction_target=80,eviction_trigger=95,eviction_updates_target=0,eviction_updates_trigger=0";
//    const char *config[] = { config0, nullptr };
//    cache_wrapper cache(session_impl, config);
//
//}