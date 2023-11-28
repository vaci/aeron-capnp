// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "serialize.h"
#include "common.h"
#include "idle.h"

#include <ImageControlledFragmentAssembler.h>
#include <capnp/serialize.h>

namespace aeroncap {

namespace {

template <typename Idler>
kj::Promise<void> writeClaim(
    ::aeron::ExclusivePublication& pub,
    kj::ArrayPtr<kj::ArrayPtr<capnp::word const> const> segments,
    uint64_t byteSize,
    Idler& idler) {

  KJ_DREQUIRE(byteSize <= pub_.maxPayloadLength());
  KJ_DREQUIRE(byteSize > 0);

  ::aeron::BufferClaim claim;

  if (auto err = pub.tryClaim(byteSize, claim); err > 0) {
    auto& buffer = claim.buffer();
    auto offset = claim.offset();
    auto array = kj::arrayPtr(buffer.buffer() + offset, byteSize);
    kj::ArrayOutputStream outputStream{array};
    capnp::writeMessage(outputStream, segments);
    claim.commit();
    return kj::READY_NOW;
  }
  else if (err == ::aeron::BACK_PRESSURED || err == ::aeron::ADMIN_ACTION) {
    return idler.idle().then(
      [&pub, segments, byteSize, &idler]() mutable {
	return writeClaim(pub, segments, byteSize, idler);
      }
    );
  }
  else {
    kj::throwFatalException(toException(err));
  }
}

template <typename Idler>
kj::Promise<void> writeOffer(
    ::aeron::ExclusivePublication& pub,
    kj::ArrayPtr<capnp::byte> bytes,
    Idler& idler) {

  KJ_DREQUIRE(bytes.size() <= pub.maxMessageLength());
  KJ_DREQUIRE(bytes.size > 0);

  if (auto err = pub.offer({bytes.begin(), bytes.size()}); err > 0) {
    return kj::READY_NOW;
  }
  else if (err == ::aeron::BACK_PRESSURED || err == ::aeron::ADMIN_ACTION) {
    return idler.idle().then(
      [&pub, bytes, &idler]() mutable {
	return writeOffer(pub, bytes, idler);
      }
    );
  }
  else {
    kj::throwFatalException(toException(err));
  }
}

template <typename Idler>
kj::Promise<kj::Maybe<kj::Own<capnp::MessageReader>>> tryReadMessage(
  Idler& idler,
  ::aeron::Image image,
  capnp::ReaderOptions options,
  kj::ArrayPtr<capnp::word> scratchSpace = nullptr,
  kj::Own<kj::VectorOutputStream> outputStream = {}) {

  using Action = ::aeron::ControlledPollAction;

  kj::Maybe<kj::Own<capnp::MessageReader>> reader;

  auto handler = [&, scratchSpace](auto& buffer, auto offset, auto length, auto& header) mutable {
    namespace frame = ::aeron::FrameDescriptor;

    auto isSet = [&header](auto bits) {
      return (header.flags() & bits) == bits;
    };

    if (isSet(frame::UNFRAGMENTED)) {
      kj::Array<capnp::word> ownedSpace;
      auto wordSize = (length+1)/sizeof(capnp::word);

      if (scratchSpace.size() < wordSize) {
	ownedSpace = kj::heapArray<capnp::word>(wordSize);
	scratchSpace = ownedSpace;
      }
      memcpy(scratchSpace.begin(), buffer.buffer() + offset, length);

      reader = kj::heap<capnp::FlatArrayMessageReader>(scratchSpace, options)
	.attach(kj::mv(ownedSpace));
      return Action::BREAK;
    }

    if (isSet(frame::BEGIN_FRAG)) {
      outputStream = kj::heap<kj::VectorOutputStream>();
    }

    outputStream->write(buffer.buffer() + offset, length);

    if (isSet(frame::END_FRAG)) {
      auto inputStream = kj::heap<kj::ArrayInputStream>(outputStream->getArray());
      reader = kj::heap<capnp::InputStreamMessageReader>(*inputStream)
	.attach(kj::mv(inputStream), kj::mv(outputStream));
      return Action::BREAK;
    }

    return Action::CONTINUE;
  };

  auto fragmentsRead = image.controlledPoll(handler, 16);
  if (reader != nullptr) {
    return kj::mv(reader);
  }

  if (KJ_UNLIKELY(image.isEndOfStream())) {
    return nullptr;
  }

  if (fragmentsRead) {
    idler.reset();
  }

  return idler.idle().then(
    [&idler, image = kj::mv(image), options, scratchSpace, outputStream = kj::mv(outputStream)]() mutable {
      return tryReadMessage(idler, kj::mv(image), options, scratchSpace, kj::mv(outputStream));
    }
  );
}

}

kj::Promise<kj::Own<capnp::MessageReader>> readMessage(
  Idler& idler,
  ::aeron::Image image,
  capnp::ReaderOptions options) {

  return tryReadMessage(idler, kj::mv(image), options)
    .then(
      [](auto maybeReader) -> kj::Promise<kj::Own<capnp::MessageReader>> {
	auto& reader = KJ_UNWRAP_OR_RETURN(maybeReader, KJ_EXCEPTION(DISCONNECTED));
	return kj::mv(reader);
      }
    );
}

AeronMessageStream::AeronMessageStream(
  ::aeron::ExclusivePublication& pub,
  ::aeron::Image image,
  Idler& readIdler,
  Idler& writeIdler)
  : pub_{pub}
  , image_{kj::mv(image)}
  , readIdler_{readIdler}
  , writeIdler_{writeIdler} {
}

AeronMessageStream::~AeronMessageStream() {
  pub_.close();
  image_.close();
}

kj::Promise<kj::Maybe<capnp::MessageReaderAndFds>> AeronMessageStream::tryReadMessage(
    kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    capnp::ReaderOptions options,
    kj::ArrayPtr<capnp::word> scratchSpace) {

  return aeroncap::tryReadMessage(readIdler_, image_, options, scratchSpace).then(
      [](auto maybeReader) -> kj::Maybe<capnp::MessageReaderAndFds> {
	auto& reader = KJ_UNWRAP_OR_RETURN(maybeReader, nullptr);
	return capnp::MessageReaderAndFds{kj::mv(reader), nullptr};
      }
  );
}

kj::Promise<void> AeronMessageStream::writeMessages(
    kj::ArrayPtr<kj::ArrayPtr<kj::ArrayPtr<capnp::word const> const>> messages) {

  auto builder = kj::heapArrayBuilder<kj::Promise<void>>(messages.size());
  for (auto msg: messages) {
    builder.add(writeMessage(nullptr, msg));
  }
  return kj::joinPromises(builder.finish());
}

kj::Promise<void> AeronMessageStream::writeMessage(
    kj::ArrayPtr<int const> fds,
    kj::ArrayPtr<kj::ArrayPtr<capnp::word const> const> segments) {

  auto wordSize = capnp::computeSerializedSizeInWords(segments);
  auto byteSize = wordSize * sizeof(capnp::word);

  KJ_DREQUIRE(byteSize > 0);
  KJ_DREQUIRE(byteSize <= pub_.maxMessageLength());

  if (byteSize <= pub_.maxPayloadLength()) {
    return writeClaim(pub_, segments, byteSize, writeIdler_);
  }
  else {
    auto words = capnp::messageToFlatArray(segments);
    return writeOffer(pub_, words.asBytes(), writeIdler_).attach(kj::mv(words));
  }
}

kj::Promise<void> AeronMessageStream::end() {
  pub_.close();
  return kj::READY_NOW;
}

kj::Maybe<int> AeronMessageStream::getSendBufferSize() {
  return pub_.termBufferLength();
}

}
