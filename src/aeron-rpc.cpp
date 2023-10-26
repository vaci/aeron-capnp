// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "aeron-rpc.h"
#include "aeron-rpc.capnp.h"
#include "common.h"
#include "serialize.h"

#include <capnp/serialize.h>
#include <kj/list.h>

namespace aeroncap {

namespace {

template <typename T>
struct Queue {
  Queue() {}

  ~Queue() {
    while (!empty()) {
      pop();
    }
  }

  bool empty() const {
    return items.empty();
  }

  size_t size() const {
    return items.size();
  }
  
  void push(T&& element) {
    auto entry = new Entry{ .item = kj::mv(element) };
    items.add(*entry);
  }

  T pop() {
    KJ_IREQUIRE(!empty());
    auto& entry = items.front();
    items.remove(entry);
    auto item = kj::mv(entry.item);
    delete &entry;
    return item;
  }

private:
  struct Entry {
    kj::ListLink<Entry> link;
    T item;
  };

  kj::List<Entry, &Entry::link> items;
};

}

namespace _ {

struct ImageReceiver {

  ImageReceiver(
    ::aeron::Aeron& aeron,
    kj::StringPtr channel,
    int32_t streamId);

  ~ImageReceiver();

  template <typename Idler>
  kj::Promise<::aeron::Image> receive(Idler idle) {
    auto queue = acceptQueue_.lockExclusive();
    if (!queue->empty()) {
      return queue->pop();
    }
    return idle()
      .then([this, idle = kj::mv(idle)]{ return receive(kj::mv(idle)); });
  }

  std::shared_ptr<::aeron::Aeron> aeron_;
  uint64_t subId_;
  kj::MutexGuarded<Queue<::aeron::Image>> acceptQueue_;
};

ImageReceiver::ImageReceiver(
  ::aeron::Aeron& aeron,
  kj::StringPtr channel,
  int32_t streamId) {
  
  subId_ = aeron.addSubscription(
    channel.cStr(),
    streamId,
    [this](auto image) {
      auto queue = acceptQueue_.lockExclusive();
      queue->push(kj::mv(image));
    },
    [](auto) {}
  );
}

ImageReceiver::~ImageReceiver() {
}

}

namespace {

template <typename Idler>
kj::Promise<void> offerMessage(
    ::aeron::ExclusivePublication& pub,
    kj::ArrayPtr<capnp::byte> bytes,
    Idler idler) {

  if (auto err = pub.offer({bytes.begin(), bytes.size()}); err > 0) {
    return kj::READY_NOW;
  }
  else if (err == ::aeron::ADMIN_ACTION || err == ::aeron::BACK_PRESSURED) {
    return idler().then([&pub, bytes, idler = kj::mv(idler)] {
      return offerMessage(pub, bytes, kj::mv(idler));
    });
  }
  else {
    kj::throwFatalException(toException(err));
  }
}

template <typename Idler>
kj::Promise<void> offerMessage(
  ::aeron::ExclusivePublication& pub,
  capnp::MessageBuilder& mb, Idler idler) {
  auto words = capnp::messageToFlatArray(mb);
  auto bytes = words.asBytes();
  return offerMessage(pub, bytes, kj::mv(idler)).attach(kj::mv(words));
}

template <typename Idler>
kj::Promise<std::shared_ptr<::aeron::ExclusivePublication>> findPublication(
  ::aeron::Aeron& aeron, int32_t pubId, Idler idle) {

  if (auto pub = aeron.findExclusivePublication(pubId)) {
    return pub;
  }

  return idle().then([&aeron, pubId, idle = kj::mv(idle)]() mutable {
    return findPublication(aeron, pubId, kj::mv(idle));
  });
}

template <typename Idler>
kj::Promise<std::shared_ptr<::aeron::ExclusivePublication>> addPublication(
  ::aeron::Aeron& aeron, kj::StringPtr channel, int32_t streamId, Idler idle) {
  auto pubId = aeron.addExclusivePublication(channel.cStr(), streamId);
  return findPublication(aeron, pubId, kj::mv(idle));
}

}

Connector::Connector(
  kj::Timer& timer,
  std::shared_ptr<::aeron::Aeron> aeron,
  kj::StringPtr channel,
  int32_t streamId)
  : aeron_{kj::mv(aeron)}
  , receiver_{kj::heap<_::ImageReceiver>(*aeron_, channel, streamId)}
  , timer_{timer}
  , tasks_{*this}
  , channel_{kj::str(channel)}
  , streamId_{streamId} {
  tasks_.add(canceler_.wrap(handleResponses()));
}

Connector::~Connector() {
  for (auto& f: fulfillers_) {
    f.value->reject(KJ_EXCEPTION(FAILED, "Connector destroyed"));
  }
}

kj::Promise<void> Connector::handleResponses() {
  return receiver_->receive(idle::periodic(timer_))
    .then(
      [this](auto image) {
	KJ_LOG(INFO, image.sourceIdentity(), image.sessionId());
	return readMessage(timer_, image)
	  .then(
	    [this, image = kj::mv(image)](auto reader) mutable {
	      auto ack = reader->template getRoot<aeron::Ack>();
	      auto sessionId = ack.getSessionId();
	      KJ_LOG(INFO, "Connector < ACK", sessionId);
                  
	      KJ_IF_MAYBE(f, fulfillers_.find(sessionId)) {
		(*f)->fulfill(kj::mv(image));
		fulfillers_.erase(sessionId);
	      }
	      else {
		// drop it like it's hot
		KJ_LOG(ERROR, "Received unknown ACK", sessionId);
	      }
	    }
	  );
      }
    )
    .catch_(
      [](auto&& exc) {
	KJ_LOG(ERROR, "Failed to accept connection", exc);
      }
    )
    .then(
      [this] {
	return timer_.afterDelay(kj::MICROSECONDS*100);
      }
    )
    .then(
      [this] {
	return handleResponses();
      }
    );
}

void Connector::taskFailed(kj::Exception&& exc) {
  KJ_LOG(ERROR, exc);
}

kj::Promise<kj::Own<AeronMessageStream>> Connector::connect(
    kj::StringPtr channel, int32_t streamId) {
  
  return addPublication(*aeron_, channel, streamId, idle::backoff(timer_))
    .then(
      [this](auto pub) {
	auto sessionId = pub->sessionId();
	auto paf = kj::newPromiseAndFulfiller<::aeron::Image>();
	fulfillers_.insert(sessionId, kj::mv(paf.fulfiller));
                  
	capnp::MallocMessageBuilder mb{capnp::sizeInWords<aeron::Syn>()};
	auto syn = mb.initRoot<aeron::Syn>();
	syn.setChannel(channel_);
	syn.setStreamId(streamId_);
	KJ_LOG(INFO, "Connector > SYN", channel_, streamId_);

	return offerMessage(*pub, mb, idle::backoff(timer_))
	  .then(
	    [this, pub = kj::mv(pub), promise = kj::mv(paf.promise)]() mutable {
	      return promise.then(
		[this, pub = kj::mv(pub)](auto image) {
		  return kj::heap<AeronMessageStream>(
		    kj::mv(pub), kj::mv(image), timer_
		  );
		}
	      );
	    }
	  );
      }
    );
}

Listener::Listener(
  kj::Timer& timer,
  std::shared_ptr<::aeron::Aeron> aeron,
  kj::StringPtr channel,
  int32_t streamId)
  : aeron_{kj::mv(aeron)}
  , receiver_{kj::heap<_::ImageReceiver>(*aeron_, channel, streamId)}
  , timer_{timer} {
}

kj::Promise<kj::Own<AeronMessageStream>> Listener::accept() {
  return receiver_->receive(idle::periodic(timer_))
    .then(
      [this](auto image) mutable {
	KJ_LOG(INFO, image.sourceIdentity(), image.sessionId());
	return readMessage(timer_, image)
	  .then(
	    [this](auto reader) mutable {
	      auto syn = reader->template getRoot<aeron::Syn>();
	      auto channel = syn.getChannel();
	      auto streamId = syn.getStreamId();
	      KJ_LOG(INFO, "Listener < SYN", channel, streamId);
	      return addPublication(*aeron_, channel, streamId, idle::backoff(timer_));
 	    }
	  )
	  .then(
	    [this, image = kj::mv(image)](auto pub) mutable {
	      auto sessionId = image.sessionId();
	      KJ_LOG(INFO, "Listener > ACK", sessionId);

	      capnp::MallocMessageBuilder mb{capnp::sizeInWords<aeron::Ack>()};
	      auto ack = mb.initRoot<aeron::Ack>();
	      ack.setSessionId(sessionId);

	      return offerMessage(*pub, mb, idle::backoff(timer_))
		.then(
		  [this, pub = kj::mv(pub), image = kj::mv(image)]() mutable {
		    return kj::heap<AeronMessageStream>(
		      kj::mv(pub), kj::mv(image), timer_
		    );
		  }
		);
	    }
	  );
      }
    );
}

TwoPartyServer::TwoPartyServer(
  capnp::Capability::Client bootstrapInterface)
  : bootstrapInterface_{kj::mv(bootstrapInterface)}
  , tasks_{*this} {
}

void TwoPartyServer::taskFailed(kj::Exception&& exc) {
  KJ_LOG(ERROR, exc);
}

struct TwoPartyServer::AcceptedConnection {

  explicit AcceptedConnection(
    capnp::Capability::Client bootstrapInterface,
    kj::Own<capnp::MessageStream> connection)
    : connection_{kj::mv(connection)}
    , network_{*connection_, capnp::rpc::twoparty::Side::SERVER}
    , rpcSystem_{capnp::makeRpcServer(network_, kj::mv(bootstrapInterface))} {
  }

  kj::Own<capnp::MessageStream> connection_;
  capnp::TwoPartyVatNetwork network_;
  capnp::RpcSystem<capnp::rpc::twoparty::VatId> rpcSystem_;
};

kj::Promise<void> TwoPartyServer::accept(AeronMessageStream& connection) {
  auto stream = kj::Own<capnp::MessageStream>(&connection, kj::NullDisposer::instance);
  auto connectionState = kj::heap<AcceptedConnection>(bootstrapInterface_, kj::mv(stream));
  return connectionState->network_.onDisconnect().attach(kj::mv(connectionState));
}

void TwoPartyServer::accept(kj::Own<AeronMessageStream> connection) {
  auto connectionState = kj::heap<AcceptedConnection>(
      bootstrapInterface_, kj::mv(connection));
  tasks_.add(connectionState->network_.onDisconnect().attach(kj::mv(connectionState)));
}

kj::Promise<void> TwoPartyServer::listen(Listener& listener) {
  return listener.accept()
    .then(
        [this, &listener](auto connection) mutable {
          accept(kj::mv(connection));
          return listen(listener);
        }
    );
}

TwoPartyClient::TwoPartyClient(AeronMessageStream& connection)
  : network_{connection, capnp::rpc::twoparty::Side::CLIENT}
  , rpcSystem_{capnp::makeRpcClient(network_)} {
}

capnp::Capability::Client TwoPartyClient::bootstrap() {
  kj::FixedArray<capnp::word, 4> scratch{};
  memset(scratch.begin(), 0, scratch.size());
  capnp::MallocMessageBuilder message(scratch);
  auto vatId = message.getRoot<capnp::rpc::twoparty::VatId>();
  vatId.setSide(
    network_.getSide() == capnp::rpc::twoparty::Side::CLIENT
    ? capnp::rpc::twoparty::Side::SERVER
    : capnp::rpc::twoparty::Side::CLIENT);
  return rpcSystem_.bootstrap(vatId);
}

}
