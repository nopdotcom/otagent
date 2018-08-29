// Copyright (c) Stash Inc. 2018 All rights reserved

#ifndef AGENT_HPP_
#define AGENT_HPP_

#include "opentxs/opentxs.hpp"

#include <string>

namespace opentxs
{

using namespace opentxs::network;

class Agent
{
public:
    Agent(
        const api::Native& app,
		const std::int64_t clients,
		const std::int64_t servers,
        const std::string& socket_path,
        const std::vector<std::string>& endpoints);

    ~Agent() = default;

private:
    const api::Native& app_;
	const std::int64_t clients_;
    const std::vector<std::string>& endpoints_;
	const std::int64_t servers_;
    const std::string& socket_path_;

    Agent() = delete;
    Agent(const Agent&) = delete;
    Agent(Agent&&) = delete;
    Agent& operator=(const Agent&) = delete;
    Agent& operator=(Agent&&) = delete;
};

}  // namespace opentxs

#endif  // AGENT_HPP_
