// Copyright (c) 2018 The Open-Transactions developers
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <thread>

#include "Agent.hpp"

#define CONFIG_SECTION "otagent"
#define CONFIG_CLIENTS "clients"
#define CONFIG_SERVERS "servers"
#define CONFIG_SERVER_PRIVKEY "server_privkey"
#define CONFIG_SERVER_PUBKEY "server_pubkey"
#define CONFIG_CLIENT_PRIVKEY "client_privkey"
#define CONFIG_CLIENT_PUBKEY "client_pubkey"

namespace fs = boost::filesystem;

#define ZAP_DOMAIN "otagent"

#define OT_METHOD "opentxs::Agent::"

namespace opentxs::agent
{
Agent::Agent(
    const api::Native& app,
    const std::int64_t clients,
    const std::int64_t servers,
    const std::string& socket_path,
    const std::vector<std::string>& endpoints,
    const std::string& serverPrivateKey,
    const std::string& serverPublicKey,
    const std::string& clientPrivateKey,
    const std::string& clientPublicKey,
    const std::string& settings_path,
    pt::ptree& config)
    : ot_(app)
    , zmq_(app.ZMQ())
    , clients_(clients)
    , internal_callback_(network::zeromq::ListenCallback::Factory(
          std::bind(&Agent::internal_handler, this, std::placeholders::_1)))
    , internal_(zmq_.DealerSocket(internal_callback_, true))
    , backend_endpoints_(backend_endpoint_generator())
    , backend_callback_(network::zeromq::ReplyCallback::Factory(
          std::bind(&Agent::backend_handler, this, std::placeholders::_1)))
    , backends_(
          create_backend_sockets(zmq_, backend_endpoints_, backend_callback_))
    , frontend_endpoints_(endpoints)
    , frontend_callback_(network::zeromq::ListenCallback::Factory(
          std::bind(&Agent::frontend_handler, this, std::placeholders::_1)))
    , frontend_(zmq_.RouterSocket(frontend_callback_, false))
    , servers_(servers)
    , settings_path_(settings_path)
    , socket_path_(socket_path)
    , config_lock_()
    , config_(config)
    , server_privkey_(serverPrivateKey)
    , server_pubkey_(serverPublicKey)
    , client_privkey_(clientPrivateKey)
    , client_pubkey_(clientPublicKey)
{
    {
        Lock lock(config_lock_);
        auto& section = config.get_child(CONFIG_SECTION);
        section.put(CONFIG_SERVER_PRIVKEY, server_privkey_);
        section.put(CONFIG_SERVER_PUBKEY, server_pubkey_);
        section.put(CONFIG_CLIENT_PRIVKEY, client_privkey_);
        section.put(CONFIG_CLIENT_PUBKEY, client_pubkey_);
        save_config(lock);
    }

    for (auto i = 0; i < servers_; ++i) {
        ot_.StartServer(ArgList(), i, false);
    }

    for (auto i = 0; i < clients_; ++i) { ot_.StartClient(ArgList(), i); }

    OT_ASSERT(0 < backend_endpoints_.size());

    auto started{false};

    for (const auto& endpoint : backend_endpoints_) {
        started = internal_->Start(endpoint);

        OT_ASSERT(started);
    }

    OT_ASSERT(false == socket_path_.empty());

    const auto zap = ot_.ZAP().RegisterDomain(
        ZAP_DOMAIN,
        std::bind(&Agent::zap_handler, this, std::placeholders::_1));

    OT_ASSERT(zap);

    const auto domain = frontend_->SetDomain(ZAP_DOMAIN);

    OT_ASSERT(domain);

    const bool set = frontend_->SetPrivateKey(server_privkey_);

    OT_ASSERT(set);

    started = frontend_->Start("ipc://" + socket_path_);

    OT_ASSERT(started);

    for (const auto& endpoint : frontend_endpoints_) {
        started = frontend_->Start(endpoint);

        OT_ASSERT(started);
    }
}

std::vector<std::string> Agent::backend_endpoint_generator()
{
    const unsigned int min_threads{1};
    const auto threads =
        std::max(std::thread::hardware_concurrency(), min_threads);
    otErr << OT_METHOD << __FUNCTION__ << ": Starting " << threads
          << " handler threads." << std::endl;
    std::vector<std::string> output{};
    const auto prefix = std::string("inproc://opentxs/agent/backend/");

    for (unsigned int i{0}; i < threads; ++i) {
        output.emplace_back(prefix + std::to_string(i));
    }

    return output;
}

OTZMQMessage Agent::backend_handler(const network::zeromq::Message& message)
{
    OT_ASSERT(0 < message.Body().size());

    const auto& frame = message.Body().at(0);
    const auto data = Data::Factory(frame.data(), frame.size());
    opentxs::proto::RPCCommand command =
        opentxs::proto::DataToProto<opentxs::proto::RPCCommand>(data);
    auto response = ot_.RPC(command);

    switch (response.type()) {
        case proto::RPCCOMMAND_ADDCLIENTSESSION: {
            update_clients();
        } break;
        case proto::RPCCOMMAND_ADDSERVERSESSION: {
            update_servers();
        } break;
        case proto::RPCCOMMAND_LISTCLIENTSSESSIONS:
        case proto::RPCCOMMAND_LISTSERVERSSESSIONS:
        case proto::RPCCOMMAND_IMPORTHDSEED:
        case proto::RPCCOMMAND_LISTHDSEEDS:
        case proto::RPCCOMMAND_GETHDSEED:
        case proto::RPCCOMMAND_CREATENYM:
        case proto::RPCCOMMAND_LISTNYMS:
        case proto::RPCCOMMAND_GETNYM:
        case proto::RPCCOMMAND_ADDCLAIM:
        case proto::RPCCOMMAND_DELETECLAIM:
        case proto::RPCCOMMAND_IMPORTSERVERCONTRACT:
        case proto::RPCCOMMAND_LISTSERVERCONTRACTS:
        case proto::RPCCOMMAND_REGISTERNYM:
        case proto::RPCCOMMAND_CREATEUNITDEFINITION:
        case proto::RPCCOMMAND_LISTUNITDEFINITIONS:
        case proto::RPCCOMMAND_ISSUEUNITDEFINITION:
        case proto::RPCCOMMAND_CREATEACCOUNT:
        case proto::RPCCOMMAND_LISTACCOUNTS:
        case proto::RPCCOMMAND_GETACCOUNTBALANCE:
        case proto::RPCCOMMAND_GETACCOUNTACTIVITY:
        case proto::RPCCOMMAND_SENDPAYMENT:
        case proto::RPCCOMMAND_MOVEFUNDS:
        case proto::RPCCOMMAND_ADDCONTACT:
        case proto::RPCCOMMAND_LISTCONTACTS:
        case proto::RPCCOMMAND_GETCONTACT:
        case proto::RPCCOMMAND_ADDCONTACTCLAIM:
        case proto::RPCCOMMAND_DELETECONTACTCLAIM:
        case proto::RPCCOMMAND_VERIFYCLAIM:
        case proto::RPCCOMMAND_ACCEPTVERIFICATION:
        case proto::RPCCOMMAND_SENDCONTACTMESSAGE:
        case proto::RPCCOMMAND_GETCONTACTACTIVITY:
        case proto::RPCCOMMAND_ERROR:
        default: {
            break;
        }
    }

    auto replymessage = network::zeromq::Message::ReplyFactory(message);
    const auto replydata =
        opentxs::proto::ProtoAsData<opentxs::proto::RPCResponse>(response);
    replymessage->AddFrame(replydata);

    return replymessage;
}

std::vector<OTZMQReplySocket> Agent::create_backend_sockets(
    const network::zeromq::Context& zmq,
    const std::vector<std::string>& endpoints,
    const OTZMQReplyCallback& callback)
{
    bool started{false};
    std::vector<OTZMQReplySocket> output{};

    for (const auto& endpoint : endpoints) {
        output.emplace_back(zmq.ReplySocket(callback, false));
        auto& socket = *output.rbegin();
        started = socket->Start(endpoint);

        OT_ASSERT(started);

        otErr << endpoint << std::endl;
    }

    return output;
}

void Agent::frontend_handler(network::zeromq::Message& message)
{
    // Forward requests to backend socket(s) via internal socket
    internal_->Send(message);
}

void Agent::increment_config_value(
    const std::string& sectionName,
    const std::string& entryName)
{
    Lock lock(config_lock_);
    pt::ptree& section = config_.get_child(sectionName);
    pt::ptree& entry = section.get_child(entryName);
    auto value = entry.get_value<std::int64_t>();
    entry.put_value<std::int64_t>(++value);
    save_config(lock);
}

void Agent::internal_handler(network::zeromq::Message& message)
{
    // Route replies back to original requestor via frontend socket
    frontend_->Send(message);
}

void Agent::save_config(const Lock& lock)
{
    fs::fstream settingsfile(settings_path_, std::ios::out);
    pt::write_ini(settings_path_, config_);
    settingsfile.close();
}

void Agent::update_clients()
{
    increment_config_value(CONFIG_SECTION, CONFIG_CLIENTS);
}

void Agent::update_servers()
{
    increment_config_value(CONFIG_SECTION, CONFIG_SERVERS);
}

OTZMQZAPReply Agent::zap_handler(const zap::Request& request) const
{
    auto output = zap::Reply::Factory(request);
    const auto& pubkey = request.Credentials().at(0);

    if (zap::Mechanism::Curve != request.Mechanism()) {
        output->SetCode(zap::Status::AuthFailure);
        output->SetStatus("Unsupported mechanism");
    } else if (client_pubkey_ != ot_.Crypto().Encode().Z85Encode(pubkey)) {
        output->SetCode(zap::Status::AuthFailure);
        output->SetStatus("Incorrect pubkey");
    } else {
        output->SetCode(zap::Status::Success);
        output->SetStatus("OK");
    }

    return output;
}
}  // namespace opentxs::agent
