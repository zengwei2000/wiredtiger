#include <catch2/catch.hpp>

#include "wt_internal.h"
#include "wrappers/mock_session.h"
#include "wrappers/block_mods.h"
#include "wrappers/block_manager_wrapper.h"

TEST_CASE("Block Manager: drop_file", "")
{
    std::shared_ptr<MockSession> session = MockSession::buildTestMockSession();
    block_manager_wrapper block_manager(session);
    REQUIRE(block_manager.drop_file("file_to_drop") == 0);
}

