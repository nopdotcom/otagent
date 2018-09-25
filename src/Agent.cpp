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
namespace zmq = opentxs::network::zeromq;

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
    , internal_callback_(zmq::ListenCallback::Factory(
          std::bind(&Agent::internal_handler, this, std::placeholders::_1)))
    , internal_(zmq_.DealerSocket(
          internal_callback_,
          zmq::Socket::Direction::Connect))
    , backend_endpoints_(backend_endpoint_generator())
    , backend_callback_(zmq::ReplyCallback::Factory(
          std::bind(&Agent::backend_handler, this, std::placeholders::_1)))
    , backends_(
          create_backend_sockets(zmq_, backend_endpoints_, backend_callback_))
    , frontend_endpoints_(endpoints)
    , frontend_callback_(zmq::ListenCallback::Factory(
          std::bind(&Agent::frontend_handler, this, std::placeholders::_1)))
    , frontend_(
          zmq_.RouterSocket(frontend_callback_, zmq::Socket::Direction::Bind))
    , servers_(servers)
    , settings_path_(settings_path)
    , socket_path_(socket_path)
    , config_lock_()
    , config_(config)
    , server_privkey_(serverPrivateKey)
    , server_pubkey_(serverPublicKey)
    , client_privkey_(clientPrivateKey)
    , client_pubkey_(clientPublicKey)
    , task_lock_()
    , task_connection_map_()
    , nym_lock_()
    , nym_connection_map_()
    , task_callback_(zmq::ListenCallback::Factory(
          std::bind(&Agent::task_handler, this, std::placeholders::_1)))
    , task_subscriber_(zmq_.SubscribeSocket(task_callback_))
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

    for (auto i = 0; i < servers_.load(); ++i) {
        ot_.StartServer(ArgList(), i, false);
    }

    for (auto i = 0; i < clients_.load(); ++i) {
        ot_.StartClient(ArgList(), i);
    }

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

    OT_ASSERT(0 <= clients_.load());

    for (int i = 1; i <= clients_.load(); ++i) {
        started = task_subscriber_->Start(
            ot_.Client(i - 1).Endpoints().TaskComplete());

        OT_ASSERT(started);
    }
}

void Agent::associate_nym(const Data& connection, const std::string& nymID)
{
    if (nymID.empty()) { return; }

    auto it = nym_connection_map_.find(nymID);

    if (nym_connection_map_.end() == it) {
        Lock lock(task_lock_);
        nym_connection_map_.emplace(nymID, connection);
        lock.unlock();
        LogOutput(OT_METHOD)(__FUNCTION__)(": Connection ")(connection.asHex())(
            " is associated with nym ")(nymID)
            .Flush();
    }
}

void Agent::associate_task(
    const Data& connection,
    const std::string& nymID,
    const std::string& task)
{
    LogOutput(OT_METHOD)(__FUNCTION__)(": Connection ")(connection.asHex())(
        " is waiting for task ")(task)
        .Flush();
    Lock lock(task_lock_);
    task_connection_map_.emplace(task, TaskData{connection, nymID});
}

std::vector<std::string> Agent::backend_endpoint_generator()
{
    const unsigned int min_threads{1};
    const auto threads =
        std::max(std::thread::hardware_concurrency(), min_threads);
    LogNormal(OT_METHOD)(__FUNCTION__)(": Starting ")(threads)(
        " handler threads.")
        .Flush();
    std::vector<std::string> output{};
    const auto prefix = std::string("inproc://opentxs/agent/backend/");

    for (unsigned int i{0}; i < threads; ++i) {
        output.emplace_back(prefix + std::to_string(i));
    }

    return output;
}

OTZMQMessage Agent::backend_handler(const zmq::Message& message)
{
    OT_ASSERT(1 < message.Body().size());

    const auto& request = message.Body().at(0);
    const auto data = Data::Factory(request.data(), request.size());
    const auto command =
        opentxs::proto::DataToProto<opentxs::proto::RPCCommand>(data);
    const auto connectionID = Data::Factory(message.Body().at(1));
    associate_nym(connectionID, command.nym());
    auto response = ot_.RPC(command);

    switch (response.type()) {
        case proto::RPCCOMMAND_ADDCLIENTSESSION: {
            update_clients();
        } break;
        case proto::RPCCOMMAND_ADDSERVERSESSION: {
            update_servers();
        } break;
        case proto::RPCCOMMAND_LISTCLIENTSESSIONS:
        case proto::RPCCOMMAND_LISTSERVERSESSIONS:
        case proto::RPCCOMMAND_IMPORTHDSEED:
        case proto::RPCCOMMAND_LISTHDSEEDS:
        case proto::RPCCOMMAND_GETHDSEED:
        case proto::RPCCOMMAND_CREATENYM: {
            for (const auto& nymid : response.identifier()) {
                associate_nym(connectionID, nymid);
            }
        } break;
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
        case proto::RPCCOMMAND_GETSERVERCONTRACT:
        case proto::RPCCOMMAND_ERROR:
        default: {
        }
    }

    if (proto::RPCRESPONSE_QUEUED == response.success()) {
        OT_ASSERT(0 == command.session() % 2);

        const auto& taskID = response.task();
        const auto& nymID = command.nym();
        associate_task(connectionID, nymID, taskID);
        // It's possible for the task subscriber to miss a task complete message
        // if the task finished quickly before we added the id to
        // task_connection_map_
        check_task(connectionID, taskID, nymID, command.session() / 2);
    }

    auto replymessage = zmq::Message::ReplyFactory(message);
    const auto replydata =
        opentxs::proto::ProtoAsData<opentxs::proto::RPCResponse>(response);
    replymessage->AddFrame(replydata);

    return replymessage;
}

void Agent::check_task(
    const Data& connectionID,
    const std::string& taskID,
    const std::string& nymID,
    const int index)
{
    const auto status =
        ot_.Client(index).Sync().Status(Identifier::Factory(taskID));
    bool result{false};

    switch (status) {
        case ThreadStatus::FINISHED_SUCCESS: {
            result = true;
        } break;
        case ThreadStatus::FINISHED_FAILED: {
            result = false;
        } break;
        case ThreadStatus::ERROR:
        case ThreadStatus::RUNNING:
        case ThreadStatus::SHUTDOWN:
        default: {
            return;
        }
    }

    Lock lock(task_lock_);
    task_connection_map_.erase(taskID);
    lock.unlock();
    send_task_push(connectionID, taskID, nymID, result);
}

std::vector<OTZMQReplySocket> Agent::create_backend_sockets(
    const zmq::Context& zmq,
    const std::vector<std::string>& endpoints,
    const OTZMQReplyCallback& callback)
{
    bool started{false};
    std::vector<OTZMQReplySocket> output{};

    for (const auto& endpoint : endpoints) {
        output.emplace_back(
            zmq.ReplySocket(callback, zmq::Socket::Direction::Bind));
        auto& socket = *output.rbegin();
        started = socket->Start(endpoint);

        OT_ASSERT(started);

        LogNormal(endpoint).Flush();
    }

    return output;
}

void Agent::frontend_handler(zmq::Message& message)
{
    const auto size = message.Header().size();

    OT_ASSERT(0 < size);

    if (0 == message.Body().size()) {
        LogOutput(OT_METHOD)(__FUNCTION__)(": Empty command.").Flush();

        return;
    }

    // Append connection identity for push notification purposes
    const auto& identity = message.Header_at(size - 1);

    OT_ASSERT(0 < identity.size());

    LogNormal(OT_METHOD)(__FUNCTION__)(": ConnectionID: ")(
        Data::Factory(identity)->asHex())
        .Flush();
    message.AddFrame(Data::Factory(identity));
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

OTZMQMessage Agent::instantiate_push(const Data& connectionID)
{
    OT_ASSERT(0 < connectionID.size());

    auto output = zmq::Message::Factory();
    output->AddFrame(connectionID);
    output->AddFrame();
    output->AddFrame("PUSH");

    OT_ASSERT(1 == output->Header().size());
    OT_ASSERT(1 == output->Body().size());

    return output;
}

void Agent::internal_handler(zmq::Message& message)
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

void Agent::send_task_push(
    const Data& connectionID,
    const std::string& taskID,
    const std::string& nymID,
    const bool result)
{
    auto push = instantiate_push(connectionID);
    proto::RPCPush message{};
    message.set_version(1);
    message.set_type(proto::RPCPUSH_TASK);
    message.set_id(nymID);
    auto& task = *message.mutable_taskcomplete();
    task.set_version(1);
    task.set_id(taskID);
    task.set_result(result);
    push->AddFrame(proto::ProtoAsData(message));
    frontend_->Send(push);
}

void Agent::task_handler(const zmq::Message& message)
{
    if (2 > message.Body().size()) {
        LogOutput(OT_METHOD)(__FUNCTION__)(": Invalid message").Flush();

        return;
    }

    LogOutput(OT_METHOD)(__FUNCTION__)(": Received notice for task ")(
        std::string(message.Body_at(0)))
        .Flush();
    const std::string taskID{message.Body_at(0)};
    const auto raw = Data::Factory(message.Body_at(1));
    bool success{false};
    OTPassword::safe_memcpy(
        &success, sizeof(success), raw->data(), raw->size());
    Lock lock(task_lock_);
    const auto it = task_connection_map_.find(taskID);

    if (task_connection_map_.end() == it) {
        LogOutput(OT_METHOD)(__FUNCTION__)(": We don't care about task ")(
            std::string(message.Body_at(0)))
            .Flush();

        return;
    }

    const OTData connectionID = it->second.first;
    const std::string nymID = it->second.second;
    task_connection_map_.erase(it);
    lock.unlock();
    send_task_push(connectionID, taskID, nymID, success);
}

void Agent::update_clients()
{
    increment_config_value(CONFIG_SECTION, CONFIG_CLIENTS);
    ++clients_;
    task_subscriber_->Start(
        ot_.Client(clients_.load() - 1).Endpoints().TaskComplete());
}

void Agent::update_servers()
{
    increment_config_value(CONFIG_SECTION, CONFIG_SERVERS);
    ++servers_;
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
