// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

#include "src/ConnectionSetupPayload.h"
#include "src/Payload.h"
#include "src/ReactiveStreamsCompat.h"
#include "src/Stats.h"

namespace reactivesocket {

class ConnectionAutomaton;
class DuplexConnection;
class RequestHandler;
class ReactiveSocket;
enum class FrameType : uint16_t;
using StreamId = uint32_t;

class KeepaliveTimer {
 public:
  virtual ~KeepaliveTimer() = default;

  virtual std::chrono::milliseconds keepaliveTime() = 0;
  virtual void stop() = 0;
  virtual void start(ConnectionAutomaton* automaton) = 0;
};

using CloseListener = std::function<void(ReactiveSocket&)>;

using ResumeSocketListener = std::function<bool(
    ReactiveSocket& newSocket,
    const ResumeIdentificationToken& token,
    ResumePosition position)>;

// TODO(stupaq): consider using error codes in place of folly::exception_wrapper

// TODO(stupaq): Here is some heavy problem with the recursion on shutdown.
// Giving someone ownership over this object would probably lead to a deadlock
// (or a crash) if one calls ::~ReactiveSocket from a terminal signal handler
// of any of the connections. It seems natural to follow ReactiveStreams
// specification and make this object "self owned", so that we don't need to
// perform all of the cleanup from a d'tor. We could give out a "proxy" object
// that (internally) holds a weak reference (conceptually, not std::weak_ptr)
// and forbids any interactions with the socket after the shutdown procedure has
// been initiated.
class ReactiveSocket {
 public:
  ReactiveSocket(ReactiveSocket&&) = delete;
  ReactiveSocket& operator=(ReactiveSocket&&) = delete;
  ReactiveSocket(const ReactiveSocket&) = delete;
  ReactiveSocket& operator=(const ReactiveSocket&) = delete;

  ~ReactiveSocket();

  static std::unique_ptr<ReactiveSocket> fromClientConnection(
      std::unique_ptr<DuplexConnection> connection,
      std::unique_ptr<RequestHandler> handler,
      ConnectionSetupPayload setupPayload = ConnectionSetupPayload(),
      Stats& stats = Stats::noop(),
      std::unique_ptr<KeepaliveTimer> keepaliveTimer =
          std::unique_ptr<KeepaliveTimer>(nullptr),
      const ResumeIdentificationToken& token = {
          {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}});

  static std::unique_ptr<ReactiveSocket> fromServerConnection(
      std::unique_ptr<DuplexConnection> connection,
      std::unique_ptr<RequestHandler> handler,
      Stats& stats = Stats::noop(),
      ResumeSocketListener resumeListener =
          [](ReactiveSocket&,
             const ResumeIdentificationToken&,
             ResumePosition) { return false; });

  Subscriber<Payload>& requestChannel(Subscriber<Payload>& responseSink);

  void requestStream(Payload payload, Subscriber<Payload>& responseSink);

  void requestSubscription(Payload payload, Subscriber<Payload>& responseSink);

  void requestFireAndForget(Payload request);

  void requestResponse(Payload payload, Subscriber<Payload>& responseSink);

  void close();

  void onClose(CloseListener listener);

  void metadataPush(std::unique_ptr<folly::IOBuf> metadata);

  void resumeFromSocket(ReactiveSocket& socket);

  void tryClientResume(
      std::unique_ptr<DuplexConnection> newConnection,
      const ResumeIdentificationToken& token);

  bool isPositionAvailable(ResumePosition position);
  ResumePosition positionDifference(ResumePosition position);

 private:
  ReactiveSocket(
      bool isServer,
      std::unique_ptr<DuplexConnection> connection,
      std::unique_ptr<RequestHandler> handler,
      ResumeSocketListener resumeListener,
      Stats& stats,
      std::unique_ptr<KeepaliveTimer> keepaliveTimer);

  bool createResponder(StreamId streamId, std::unique_ptr<folly::IOBuf> frame);
  bool resumeListener(
      const ResumeIdentificationToken& token,
      ResumePosition position);

  const std::shared_ptr<ConnectionAutomaton> connection_;
  std::unique_ptr<RequestHandler> handler_;
  StreamId nextStreamId_;
  std::unique_ptr<KeepaliveTimer> keepaliveTimer_;
  ResumeSocketListener resumeSocketListener_;
};
}
