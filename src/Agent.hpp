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
        const std::vector<std::string>& endpoints,
		const std::string& settings_path);

    ~Agent() = default;

private:
    const api::Native& app_;
    const zeromq::Context& zmq_;
	const std::int64_t clients_;
    const OTZMQListenCallback dealer_callback_;
    const OTZMQDealerSocket dealer_;
    const std::vector<std::string>& endpoints_;
    const OTZMQReplyCallback reply_callback_;
    const OTZMQReplySocket reply_;
    const OTZMQListenCallback router_callback_;
    const OTZMQRouterSocket router_;
	const std::int64_t servers_;
	const std::string& settings_path_;
    const std::string& socket_path_;
    
    void dealer_handler(zeromq::Message& message);
    OTZMQMessage reply_handler(const zeromq::Message& message);
    void router_handler(zeromq::Message& message);
    void update_clients();
    void update_servers();

    Agent() = delete;
    Agent(const Agent&) = delete;
    Agent(Agent&&) = delete;
    Agent& operator=(const Agent&) = delete;
    Agent& operator=(Agent&&) = delete;
};

}  // namespace opentxs

#endif  // AGENT_HPP_
