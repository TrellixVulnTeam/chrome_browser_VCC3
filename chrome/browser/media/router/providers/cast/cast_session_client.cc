// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/router/providers/cast/cast_session_client.h"

#include "base/bind.h"
#include "chrome/browser/media/router/data_decoder_util.h"
#include "chrome/browser/media/router/providers/cast/cast_activity_record.h"

using blink::mojom::PresentationConnectionCloseReason;
using blink::mojom::PresentationConnectionMessagePtr;
using blink::mojom::PresentationConnectionPtrInfo;
using blink::mojom::PresentationConnectionState;

namespace media_router {

namespace {

void ReportClientMessageParseError(const MediaRoute::Id& route_id,
                                   const std::string& error) {
  // TODO(crbug.com/808720): Record UMA metric for parse result.
  DVLOG(2) << "Failed to parse Cast client message for " << route_id << ": "
           << error;
}

}  // namespace

CastSessionClient::CastSessionClient(const std::string& client_id,
                                     const url::Origin& origin,
                                     int tab_id,
                                     AutoJoinPolicy auto_join_policy,
                                     DataDecoder* data_decoder,
                                     CastActivityRecord* activity)
    : client_id_(client_id),
      origin_(origin),
      tab_id_(tab_id),
      auto_join_policy_(auto_join_policy),
      data_decoder_(data_decoder),
      activity_(activity),
      connection_binding_(this),
      weak_ptr_factory_(this) {}

CastSessionClient::~CastSessionClient() = default;

mojom::RoutePresentationConnectionPtr CastSessionClient::Init() {
  PresentationConnectionPtrInfo renderer_connection;
  connection_binding_.Bind(mojo::MakeRequest(&renderer_connection));
  auto connection_request = mojo::MakeRequest(&connection_);
  connection_->DidChangeState(PresentationConnectionState::CONNECTED);
  return mojom::RoutePresentationConnection::New(std::move(renderer_connection),
                                                 std::move(connection_request));
}

void CastSessionClient::SendMessageToClient(
    PresentationConnectionMessagePtr message) {
  connection_->OnMessage(std::move(message));
}

void CastSessionClient::SendMediaStatusToClient(
    const base::Value& media_status,
    base::Optional<int> request_id) {
  // Look up if there is a pending request from this client associated with this
  // message. If so, send the media status message as a response by setting the
  // sequence number.
  base::Optional<int> sequence_number;
  if (request_id) {
    auto it = pending_media_requests_.find(*request_id);
    if (it != pending_media_requests_.end()) {
      DVLOG(2) << "Found matching request id: " << *request_id << " -> "
               << it->second;
      sequence_number = it->second;
      pending_media_requests_.erase(it);
    }
  }

  SendMessageToClient(
      CreateV2Message(client_id_, media_status, sequence_number));
}

void CastSessionClient::OnMessage(PresentationConnectionMessagePtr message) {
  if (!message->is_message())
    return;

  data_decoder_->ParseJson(
      message->get_message(),
      base::BindRepeating(&CastSessionClient::HandleParsedClientMessage,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&ReportClientMessageParseError,
                          activity_->route().media_route_id()));
}

void CastSessionClient::DidClose(PresentationConnectionCloseReason reason) {
  // TODO(https://crbug.com/809249): Implement close connection with this
  // method once we make sure Blink calls this on navigation and on
  // PresentationConnection::close().
}
bool CastSessionClient::MatchesAutoJoinPolicy(url::Origin origin,
                                              int tab_id) const {
  switch (auto_join_policy_) {
    case AutoJoinPolicy::kTabAndOriginScoped:
      return origin == origin_ && tab_id == tab_id_;
    case AutoJoinPolicy::kOriginScoped:
      return origin == origin_;
    default:
      return false;
  }
}

void CastSessionClient::HandleParsedClientMessage(
    std::unique_ptr<base::Value> message) {
  std::unique_ptr<CastInternalMessage> cast_message =
      CastInternalMessage::From(std::move(*message));
  if (!cast_message) {
    ReportClientMessageParseError(activity_->route().media_route_id(),
                                  "Not a Cast message");
    DLOG(ERROR) << "Received non-Cast message from client";
    return;
  }

  if (cast_message->client_id != client_id_) {
    DLOG(ERROR) << "Client ID mismatch: expected: " << client_id_
                << ", got: " << cast_message->client_id;
    return;
  }

  if (cast_message->has_session_id() &&
      cast_message->session_id() != activity_->session_id()) {
    DLOG(ERROR) << "Session ID mismatch: expected: "
                << activity_->session_id().value_or("<missing>")
                << ", got: " << cast_message->session_id();
    return;
  }

  switch (cast_message->type) {
    case CastInternalMessage::Type::kAppMessage:
      // Send an ACK message back to SDK client to indicate it is handled.
      if (activity_->SendAppMessageToReceiver(*cast_message) ==
          cast_channel::Result::kOk) {
        DCHECK(cast_message->sequence_number);
        SendMessageToClient(CreateAppMessageAck(
            cast_message->client_id, *cast_message->sequence_number));
      }
      break;

    case CastInternalMessage::Type::kV2Message:
      HandleV2ProtocolMessage(*cast_message);
      break;

    case CastInternalMessage::Type::kLeaveSession:
      SendMessageToClient(CreateLeaveSessionAckMessage(
          client_id_, cast_message->sequence_number));
      activity_->HandleLeaveSession(client_id_);
      break;

    default:
      DLOG(ERROR) << "Unhandled message type: "
                  << static_cast<int>(cast_message->type);
  }
}

void CastSessionClient::HandleV2ProtocolMessage(
    const CastInternalMessage& cast_message) {
  const std::string& type_str = cast_message.v2_message_type();
  cast_channel::V2MessageType type =
      cast_channel::V2MessageTypeFromString(type_str);
  if (cast_channel::IsMediaRequestMessageType(type)) {
    DVLOG(2) << "Got media command from client: " << type_str;
    base::Optional<int> request_id =
        activity_->SendMediaRequestToReceiver(cast_message);
    if (request_id) {
      DCHECK(cast_message.sequence_number);
      if (pending_media_requests_.size() >= kMaxPendingMediaRequests) {
        // Delete old pending requests.  Request IDs are generated sequentially,
        // so this should always delete the oldest requests.  Deleting requests
        // is O(n) in the size of the table, so we delete half the outstanding
        // requests at once so the amortized deletion cost is O(1).
        pending_media_requests_.erase(pending_media_requests_.begin(),
                                      pending_media_requests_.begin() +
                                          pending_media_requests_.size() / 2);
      }
      pending_media_requests_.emplace(*request_id,
                                      *cast_message.sequence_number);
    }
  } else if (type == cast_channel::V2MessageType::kSetVolume) {
    DVLOG(2) << "Got volume command from client";
    DCHECK(cast_message.sequence_number);
    activity_->SendSetVolumeRequestToReceiver(
        cast_message, base::BindOnce(&CastSessionClient::SendResultResponse,
                                     weak_ptr_factory_.GetWeakPtr(),
                                     *cast_message.sequence_number));
  } else if (type == cast_channel::V2MessageType::kStop) {
    // TODO(jrw): implement STOP_SESSION.
    DVLOG(2) << "Ignoring stop-session (" << type_str << ") message";
  } else {
    DLOG(ERROR) << "Unknown v2 message type: " << type_str;
  }
}

void CastSessionClient::SendResultResponse(int sequence_number,
                                           cast_channel::Result result) {
  // TODO(jrw): Send error message on failure.
  if (result == cast_channel::Result::kOk) {
    // Send an empty message to let the client know the request succeeded.
    SendMessageToClient(
        CreateV2Message(client_id_, base::Value(), sequence_number));
  }
}

void CastSessionClient::CloseConnection(
    PresentationConnectionCloseReason close_reason) {
  if (connection_)
    connection_->DidClose(close_reason);

  TearDownPresentationConnection();
}

void CastSessionClient::TerminateConnection() {
  if (connection_) {
    connection_->DidChangeState(PresentationConnectionState::TERMINATED);
  }

  TearDownPresentationConnection();
}

void CastSessionClient::TearDownPresentationConnection() {
  connection_.reset();
  connection_binding_.Close();
}

}  // namespace media_router
