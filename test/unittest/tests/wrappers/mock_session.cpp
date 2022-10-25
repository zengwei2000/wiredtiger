/*-
 * Copyright (c) 2014-present MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *      All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include <utility>

#include "mock_session.h"
#include "mock_connection.h"
#include "../utils.h"

MockSession::MockSession(WT_SESSION_IMPL *session, std::shared_ptr<MockConnection> mockConnection)
    : _sessionImpl(session), _mockConnection(std::move(mockConnection))
{
#if defined(_MSC_VER)
    utils::throwIfNonZero(__wt_os_win(session));
#else
    utils::throwIfNonZero(__wt_os_posix(session));
#endif
}

MockSession::~MockSession()
{
    std::ignore = __wt_cache_destroy(_sessionImpl);
    __wt_free(nullptr, S2FS(_sessionImpl));
    __wt_free(nullptr, _sessionImpl);
}

std::shared_ptr<MockSession>
MockSession::buildTestMockSession()
{
    auto mockConnection = MockConnection::buildTestMockConnection();

    WT_SESSION_IMPL *sessionImpl = nullptr;
    utils::throwIfNonZero(__wt_calloc(nullptr, 1, sizeof(WT_SESSION_IMPL), &sessionImpl));
    sessionImpl->iface.connection = mockConnection->getWtConnection();

    // Construct an object that will now own the two pointers passed in.
    return std::shared_ptr<MockSession>(new MockSession(sessionImpl, mockConnection));
}
