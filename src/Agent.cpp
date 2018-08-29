// Copyright (c) Stash Inc. 2018 All rights reserved

#include "Agent.hpp"

//#define OT_METHOD "opentxs::Agent::"

namespace opentxs
{

Agent::Agent(
    const api::Native& app,
	const std::int64_t clients,
	const std::int64_t servers,
    const std::string& socket_path,
    const std::vector<std::string>& endpoints)
    : app_(app)
	, clients_{clients}
	, endpoints_{endpoints}
	, servers_{servers}
	, socket_path_{socket_path}
{
}

}  // namespace opentxs
