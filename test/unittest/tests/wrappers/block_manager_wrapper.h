#pragma once

#include <string>

#include "mock_session.h"

class block_manager_wrapper {
    public:
    block_manager_wrapper(std::shared_ptr<MockSession>& session);
    ~block_manager_wrapper();
    int drop_file(std::string const& file_name);

    private:
    std::shared_ptr<MockSession> _session;
};

