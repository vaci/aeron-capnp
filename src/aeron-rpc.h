#pragma once
// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "queue.h"
#include "serialize.h"

#include <capnp/capability.h>
#include <capnp/rpc-twoparty.h>
#include <capnp/serialize-async.h>
#include <kj/async-io.h>
#include <kj/io.h>
#include <kj/map.h>

#include <Aeron.h>

#include <queue>

namespace aeroncap {

namespace _ {
struct ImageReceiver;
KJ_DECLARE_NON_POLYMORPHIC(ImageReceiver);
}

struct Connector
  : private kj::TaskSet::ErrorHandler {

  Connector(
    kj::Timer&,
    std::shared_ptr<::aeron::Aeron>,
    kj::StringPtr channel,
    int32_t streamId);

  ~Connector();

  kj::Promise<kj::Own<AeronMessageStream>> connect(
      kj::StringPtr channel, int32_t streamId);

private:
  void taskFailed(kj::Exception&&) override;
  kj::Promise<void> handleResponses();

  std::shared_ptr<::aeron::Aeron> aeron_;
  kj::Own<_::ImageReceiver> receiver_;
  kj::Timer& timer_;
  kj::Canceler canceler_;
  kj::TaskSet tasks_;
  kj::String channel_;
  int32_t streamId_;

  kj::HashMap<int32_t, kj::Own<kj::PromiseFulfiller<::aeron::Image>>> fulfillers_;
};

struct Listener {

  Listener(
    kj::Timer&,
    std::shared_ptr<::aeron::Aeron>,
    kj::StringPtr channel,
    int32_t streamId);

  kj::Promise<kj::Own<AeronMessageStream>> accept();

  std::shared_ptr<::aeron::Aeron> aeron_;
  kj::Own<_::ImageReceiver> receiver_;
  kj::Timer& timer_;
};

struct TwoPartyServer
  : private kj::TaskSet::ErrorHandler {

  explicit TwoPartyServer(capnp::Capability::Client bootstrapInterface);

  kj::Promise<void> accept(AeronMessageStream&);
  void accept(kj::Own<AeronMessageStream>);

  kj::Promise<void> listen(Listener& listener);
  kj::Promise<void> drain() { return tasks_.onEmpty(); }

private:
  void taskFailed(kj::Exception&&) override;

  capnp::Capability::Client bootstrapInterface_;
  kj::TaskSet tasks_;

  struct AcceptedConnection;
};

struct TwoPartyClient {
  explicit TwoPartyClient(AeronMessageStream&);
  capnp::Capability::Client bootstrap();

private:
  capnp::TwoPartyVatNetwork network_;
  capnp::RpcSystem<capnp::rpc::twoparty::VatId> rpcSystem_;
};

}
