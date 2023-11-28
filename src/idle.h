#pragma once
// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include <kj/async.h>
#include <kj/debug.h>
#include <kj/exception.h>
#include <kj/timer.h>

namespace aeroncap {

struct Idler {
  virtual ~Idler() = default;
  virtual kj::Promise<void> idle() = 0;
  virtual void reset();
};

namespace idle {

inline auto backoff(
  kj::Timer& timer,
  kj::Duration delay = kj::NANOSECONDS,
  uint16_t count = 16, // 65.536μs
  uint16_t spin = 3) {

  struct BackoffIdler
    : Idler {

    BackoffIdler(kj::Timer& timer, kj::Duration delay, uint16_t count, uint16_t spin)
      : timer_{timer}
      , delay_{delay}
      , count_{count}
      , spin_{spin} {
      reset();
    }

    kj::Promise<void> idle() override {
      if (currentSpin_) {
	--currentSpin_;
	return kj::evalLater([]{});
      }
      auto promise = timer_.afterDelay(currentDelay_);
      if (currentCount_) {
	--currentCount_;
	currentDelay_ *= 2;
      }
      return promise;
    }

    void reset() override {
      currentDelay_ = delay_;
      currentCount_ = count_;
      currentSpin_ = spin_;
    }

    kj::Timer& timer_;
    kj::Duration delay_;
    uint16_t count_;
    uint16_t spin_;
    uint16_t currentCount_;
    uint16_t currentSpin_;
    kj::Duration currentDelay_;
  };

  return BackoffIdler{timer, delay, count, spin};
}

inline auto yield(uint64_t count = kj::maxValue) {

  struct YieldIdler
    : Idler {

    YieldIdler(uint64_t count)
      : count_{count} {
      reset();
    }

    kj::Promise<void> idle() override {
      if (currentCount_ == 0) return KJ_EXCEPTION(OVERLOADED);
      --currentCount_;
      return kj::evalLast([]{});
    }

    void reset() override {
      currentCount_ = count_;
    }

    uint64_t count_;
    uint64_t currentCount_;
  };
  return YieldIdler{count};
}

inline auto periodic(
  kj::Timer& timer,
  kj::Duration period = kj::MILLISECONDS,
  uint64_t count = kj::maxValue) {

  struct PeriodicIdler
    : Idler {

    PeriodicIdler(kj::Timer& timer, kj::Duration period, uint64_t count)
      : timer_{timer}
      , period_{period}
      , count_{count} {
      reset();
    }

    kj::Promise<void> idle() override {
      if (currentCount_ == 0) return KJ_EXCEPTION(OVERLOADED);
      currentCount_--;
      return timer_.afterDelay(period_);
    }

    void reset() override {
      currentCount_ = count_;
    }

    kj::Timer& timer_;
    kj::Duration period_;
    uint64_t count_;
    uint64_t currentCount_;
  };
  return PeriodicIdler{timer, period, count};
}

}

}
