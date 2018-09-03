// Copyright (c) 2018 The Open-Transactions developers
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef AGENT_HPP_
#define AGENT_HPP_

#include "opentxs/opentxs.hpp"

#include <string>

namespace opentxs::agent
{
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
    const network::zeromq::Context& zmq_;
    const std::int64_t clients_;
    const OTZMQListenCallback internal_callback_;
    const OTZMQDealerSocket internal_;
    const std::vector<std::string> backend_endpoints_;
    const OTZMQReplyCallback backend_callback_;
    const std::vector<OTZMQReplySocket> backends_;
    const std::vector<std::string>& frontend_endpoints_;
    const OTZMQListenCallback frontend_callback_;
    const OTZMQRouterSocket frontend_;
    const std::int64_t servers_;
    const std::string& settings_path_;
    const std::string& socket_path_;

    static std::vector<std::string> backend_endpoint_generator();
    static std::vector<OTZMQReplySocket> create_backend_sockets(
        const network::zeromq::Context& zmq,
        const std::vector<std::string>& endpoints,
        const OTZMQReplyCallback& callback);

    void internal_handler(network::zeromq::Message& message);
    OTZMQMessage backend_handler(const network::zeromq::Message& message);
    void frontend_handler(network::zeromq::Message& message);
    void update_clients();
    void update_servers();

    Agent() = delete;
    Agent(const Agent&) = delete;
    Agent(Agent&&) = delete;
    Agent& operator=(const Agent&) = delete;
    Agent& operator=(Agent&&) = delete;
};
}  // namespace opentxs::agent
#endif  // AGENT_HPP_
