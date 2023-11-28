// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "aeron-rpc.h"
#include "hello.capnp.h"

#include <Aeron.h>
#include <aeronmd/aeronmd.h>

#include <capnp/message.h>
#include <capnp/rpc-twoparty.h>

#include <kj/debug.h>
#include <kj/main.h>

#include <gtest/gtest.h>

static int EKAM_TEST_DISABLE_INTERCEPTOR = 1;

using namespace aeroncap;

struct AeronRpc
  : testing::Test {

  AeronRpc();
  ~AeronRpc() noexcept;


  bool isRunning() {
    auto running = running_.lockExclusive();
    return *running;
  }

  static void terminationHook(void *state) {
    KJ_LOG(INFO, "Termination hook called");
    auto running = static_cast<AeronRpc*>(state)->running_.lockExclusive();
    *running = false;
  }

  std::shared_ptr<::aeron::ExclusivePublication> newPublisher(int streamId) {
    auto id = aeron_->addExclusivePublication("aeron:ipc", streamId);
    auto pub = aeron_->findExclusivePublication(id);
    while (pub == nullptr) {
      pub = aeron_->findExclusivePublication(id);
    }
    return pub;
  }

  std::shared_ptr<::aeron::Subscription> newSubscriber(int streamId) {
    auto id = aeron_->addSubscription("aeron:ipc", streamId);
    auto sub = aeron_->findSubscription(id);
    while (sub == nullptr) {
      sub = aeron_->findSubscription(id);
    }
    return sub;
  }

  kj::AsyncIoContext ioCtx_{kj::setupAsyncIo()};
  kj::WaitScope& waitScope_{ioCtx_.waitScope};
  kj::Timer& timer_{ioCtx_.provider->getTimer()};

  kj::String driverPath_{kj::str("/tmp/aeron-driver.XXXXXX"_kj)};
  aeron_driver_context_t* driverContext_{};
  aeron_driver_t* driver_{};
  kj::Own<kj::Thread> runner_;

  std::shared_ptr<::aeron::Aeron> aeron_;

  kj::MutexGuarded<bool> running_;

  const int count_ = 256;
  const int ttl_ = 0;
};

AeronRpc::AeronRpc() {
  //  waitScope_.setBusyPollInterval(32);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
  mkdtemp(driverPath_.begin());
#pragma GCC diagnostic pop
  KJ_LOG(INFO, driverPath_);

  if (aeron_driver_context_init(&driverContext_)) {
    auto errcode = aeron_errcode();
    auto errmsg = aeron_errmsg();
    KJ_FAIL_REQUIRE("aeron_driver_context_init", errcode, errmsg);
  }

  aeron_driver_context_set_print_configuration(driverContext_, false);
  aeron_driver_context_set_threading_mode(
      driverContext_, AERON_THREADING_MODE_DEDICATED);
  aeron_driver_context_set_dir(driverContext_, driverPath_.cStr());
  aeron_driver_context_set_dir_delete_on_start(driverContext_, true);
  aeron_driver_context_set_dir_delete_on_shutdown(driverContext_, true);
  aeron_driver_context_set_driver_termination_hook(
      driverContext_, terminationHook, nullptr);

  if (aeron_driver_init(&driver_, driverContext_)) {
    auto errcode = aeron_errcode();
    auto errmsg = aeron_errmsg();
    KJ_FAIL_REQUIRE("aeron_driver_init", errcode, errmsg);
  }
  if (aeron_driver_start(driver_, true)) {
    auto errcode = aeron_errcode();
    auto errmsg = aeron_errmsg();
    KJ_FAIL_REQUIRE("aeron_driver_start", errcode, errmsg);
  }
  {
    auto running = running_.lockExclusive();
    *running = true;
  }
  runner_ = kj::heap<kj::Thread>(
    [this]{
      while (isRunning()) {
	auto count = aeron_driver_main_do_work(driver_);
	aeron_driver_main_idle_strategy(driver_, count);
      }
    }
  );
  {
    ::aeron::Context aeronContext;
    aeronContext.aeronDir(driverPath_.cStr());
    aeron_ = ::aeron::Aeron::connect(aeronContext);
  }
}

AeronRpc::~AeronRpc() noexcept {
  {
    auto running = running_.lockExclusive();
    *running = false;
  }
  if (runner_) {
    runner_ = nullptr;
  }
  if (driver_ && aeron_driver_close(driver_)) {
    auto errcode = aeron_errcode();
    auto errmsg = aeron_errmsg();
    KJ_LOG(ERROR, "aeron_driver_close", errcode, errmsg);
  }
  if (driverContext_ && aeron_driver_context_close(driverContext_)) {
    auto errcode = aeron_errcode();
    auto errmsg = aeron_errmsg();
    KJ_LOG(ERROR, "aeron_driver_context_close", errcode, errmsg);
  }
}

TEST_F(AeronRpc, Basic) {
  auto subA = newSubscriber(1);
  auto pubA = newPublisher(1);
  auto subB = newSubscriber(2);
  auto pubB = newPublisher(2);
  auto imageA = subA->imageByIndex(0);
  auto imageB = subB->imageByIndex(0);
  auto msA = AeronMessageStream(*pubA, *imageB, timer_);
  auto msB = AeronMessageStream(*pubB, *imageA, timer_);

  capnp::MallocMessageBuilder mb;
  auto data = mb.initRoot<capnp::Text>(16u);
  memset(data.begin(), 'a', data.size());

  msA.writeMessage(nullptr, mb.getSegmentsForOutput()).wait(waitScope_);
  auto msg = msB.readMessage().wait(waitScope_);
  auto txt = msg->getRoot<capnp::Text>();
  EXPECT_EQ(txt.size(), data.size());
}

struct HelloServer
  : Hello::Server {

  kj::Promise<void> greet(GreetContext ctx) {
    KJ_LOG(INFO, "Hello?");
    auto reply = ctx.getResults();
    reply.setGreeting("Hello, world!"_kj);
    return kj::READY_NOW;
  }
};

TEST_F(AeronRpc, RpcService) {
  auto subA = newSubscriber(1);
  auto pubA = newPublisher(1);
  auto subB = newSubscriber(2);
  auto pubB = newPublisher(2);
  auto imageA = subA->imageByIndex(0);
  auto imageB = subB->imageByIndex(0);
  auto msA = AeronMessageStream(*pubA, *imageB, timer_);
  auto msB = AeronMessageStream(*pubB, *imageA, timer_);

  capnp::TwoPartyVatNetwork server{msB, capnp::rpc::twoparty::Side::SERVER};
  auto rpcServer = capnp::makeRpcServer(server, kj::heap<HelloServer>());

  capnp::TwoPartyVatNetwork client{msA, capnp::rpc::twoparty::Side::CLIENT};
  auto rpcClient = capnp::makeRpcClient(client);

  capnp::MallocMessageBuilder mb;
  auto vatId = mb.getRoot<capnp::rpc::twoparty::VatId>();
  vatId.setSide(capnp::rpc::twoparty::Side::SERVER);
  {
    auto server = rpcClient.bootstrap(vatId).castAs<Hello>();
    auto req = server.greetRequest();
    auto reply = req.send().wait(waitScope_);
    KJ_LOG(INFO, reply);
  }
}

TEST_F(AeronRpc, TwoParty) {
  Listener listener{timer_, aeron_, "aeron:ipc", 1};
  Connector connector{timer_, aeron_, "aeron:ipc", 2};  
  TwoPartyServer server{kj::heap<HelloServer>()};
  auto listening = server.listen(listener);
  auto connection = connector.connect("aeron:ipc", 1).wait(waitScope_);
  TwoPartyClient client{*connection};
  auto cap = client.bootstrap().castAs<Hello>();
  auto req = cap.greetRequest();
  req.send().wait(waitScope_);
}

int main(int argc, char* argv[]) {
  kj::TopLevelProcessContext processCtx{argv[0]};
  processCtx.increaseLoggingVerbosity();

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

