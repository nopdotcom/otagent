// Copyright (c) 2018 The Open-Transactions developers
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "Agent.hpp"

#define CONFIG_SECTION "otagent"
#define CONFIG_CLIENTS "clients"
#define CONFIG_SERVERS "servers"

namespace pt = boost::property_tree;
namespace fs = boost::filesystem;

//#define OT_METHOD "opentxs::Agent::"

namespace opentxs {

Agent::Agent(const api::Native &app, const std::int64_t clients,
             const std::int64_t servers, const std::string &socket_path,
             const std::vector<std::string> &endpoints,
             const std::string &settings_path)
    : app_(app), zmq_(app.ZMQ()), clients_{clients},
      dealer_callback_{zeromq::ListenCallback::Factory(
          std::bind(&Agent::dealer_handler, this, std::placeholders::_1))},
      dealer_{zmq_.DealerSocket(dealer_callback_.get(), true)},
      endpoints_{endpoints}, reply_callback_{zeromq::ReplyCallback::Factory(
                                 std::bind(&Agent::reply_handler, this,
                                           std::placeholders::_1))},
      reply_{zmq_.ReplySocket(reply_callback_.get(), false)},
      router_callback_{zeromq::ListenCallback::Factory(
          std::bind(&Agent::router_handler, this, std::placeholders::_1))},
      router_{zmq_.RouterSocket(router_callback_.get(), false)},
      servers_{servers}, settings_path_{settings_path}, socket_path_{
                                                            socket_path} {
  OT_ASSERT(false == socket_path_.empty());

  auto started = router_->Start("ipc://" + socket_path_);

  OT_ASSERT(started);

  for (auto &ep : endpoints_) {
    started = router_->Start(ep);

    OT_ASSERT(started);
  }

  for (auto i = 0; i < servers_; ++i) {
    app_.StartServer(ArgList(), i, false);
  }

  for (auto i = 0; i < clients_; ++i) {
    app_.StartClient(ArgList(), i);
  }
}

void Agent::dealer_handler(zeromq::Message &message) { router_->Send(message); }

OTZMQMessage Agent::reply_handler(const zeromq::Message &message) {
  OT_ASSERT(1 > message.Body().size());

  const auto &frame = message.Body().at(0);
  const auto data = Data::Factory(frame.data(), frame.size());
  opentxs::proto::RPCCommand command =
      opentxs::proto::DataToProto<opentxs::proto::RPCCommand>(data);

  auto response = app_.RPC(command);

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
  default:
    break;
  }

  auto replymessage = zeromq::Message::ReplyFactory(message);
  const auto replydata =
      opentxs::proto::ProtoAsData<opentxs::proto::RPCResponse>(response);
  replymessage->AddFrame(replydata);

  return replymessage;
}

void Agent::router_handler(zeromq::Message &message) { dealer_->Send(message); }

void Agent::update_clients() {
  pt::ptree pt;

  try {
    pt::read_ini(settings_path_, pt);
  } catch (pt::ini_parser_error &e) {
    std::cerr << "ERROR: " << e.what() << "\n\n" << std::endl;
  }

  pt::ptree &section = pt.get_child(CONFIG_SECTION);
  pt::ptree &entry = section.get_child(CONFIG_CLIENTS);
  auto clients = entry.get_value<std::int64_t>();
  entry.put_value<std::int64_t>(++clients);
  pt::write_ini(settings_path_, pt);
}

void Agent::update_servers() {
  pt::ptree pt;

  try {
    pt::read_ini(settings_path_, pt);
  } catch (pt::ini_parser_error &e) {
    std::cerr << "ERROR: " << e.what() << "\n\n" << std::endl;
  }

  pt::ptree &section = pt.get_child(CONFIG_SECTION);
  pt::ptree &entry = section.get_child(CONFIG_SERVERS);
  auto servers = entry.get_value<std::int64_t>();
  entry.put_value<std::int64_t>(++servers);
  pt::write_ini(settings_path_, pt);
}
} // namespace opentxs
