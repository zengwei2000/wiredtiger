#include "block_manager_wrapper.h"
#include "wt_internal.h"
#include "mock_session.h"

block_manager_wrapper::block_manager_wrapper(std::shared_ptr<MockSession>& session)
   : _session(session)
{
}

block_manager_wrapper::~block_manager_wrapper() {
}


int
block_manager_wrapper::drop_file(std::string const& file_name)
{
    return __wt_block_manager_drop(_session->getWtSessionImpl(), file_name.c_str(), true);
}
