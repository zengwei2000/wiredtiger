#pragma once

#include "wt_internal.h"

class cache_wrapper {
    public:
    cache_wrapper(WT_SESSION_IMPL *session_impl, const char *config[]);

    private:
    // _session is a raw pointer, as we don't own it
    WT_SESSION_IMPL *_session_impl;
};

