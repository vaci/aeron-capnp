#pragma once
// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "idle.h"

#include <Aeron.h>
#include <capnp/serialize-async.h>

namespace aeroncap {

  kj::Promise<kj::Own<capnp::MessageReader>> readMessage(
    kj::Timer&,
    ::aeron::Image image,
    capnp::ReaderOptions options = {}
  );

  struct AeronMessageStream final
  : capnp::MessageStream {

  AeronMessageStream(
    std::shared_ptr<::aeron::ExclusivePublication>,
    ::aeron::Image,
    kj::Function<Idler()> idlerFactory
  );

  AeronMessageStream(
    std::shared_ptr<::aeron::ExclusivePublication> pub,
    ::aeron::Image image,
    kj::Timer& timer)
    : AeronMessageStream{
	kj::mv(pub),
	kj::mv(image),
	[&timer]{ return idle::backoff(timer); }
      } {
  }

  ~AeronMessageStream();

  kj::Promise<kj::Maybe<capnp::MessageReaderAndFds>> tryReadMessage(
    kj::ArrayPtr<kj::AutoCloseFd>,
    capnp::ReaderOptions = capnp::ReaderOptions{},
    kj::ArrayPtr<capnp::word> scratchSpace = nullptr) override;

  kj::Promise<void> writeMessage(
    kj::ArrayPtr<int const>,
    kj::ArrayPtr<kj::ArrayPtr<capnp::word const> const>) override;

  kj::Promise<void> writeMessages(
    kj::ArrayPtr<kj::ArrayPtr<kj::ArrayPtr<capnp::word const> const>>) override;

  kj::Promise<void> end() override;

  kj::Maybe<int> getSendBufferSize() override;

private:
  std::shared_ptr<::aeron::ExclusivePublication> pub_;
  ::aeron::Image image_;
  kj::Function<Idler()> idlerFactory_;
};

}