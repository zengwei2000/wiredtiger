#include "cache_wrapper.h"
#include "../utils.h"

cache_wrapper::cache_wrapper(WT_SESSION_IMPL *session_impl, const char *config[])
 : _session_impl(session_impl)
{
    int ret = __wt_cache_create(_session_impl, config);
    utils::throwIfNonZero(ret);
}
