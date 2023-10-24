#pragma once
// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include <kj/async.h>
#include <kj/debug.h>
#include <kj/exception.h>
#include <kj/timer.h>

namespace aeroncap {

using Idler = kj::Function<kj::Promise<void>()>;
  
namespace idle {

inline auto backoff(
  kj::Timer& timer,
  kj::Duration delay = kj::NANOSECONDS,
  uint64_t count = 16, // 65.536μs
  uint64_t spin = 3) {

  return [&timer, delay, count, spin]() mutable -> kj::Promise<void> {
    if (spin) {
      --spin;
      return kj::evalLater([]{});
    }

    auto promise = timer.afterDelay(delay);
    if (count) {
      --count;
      delay *= 2;
    }
    return promise;
  };
}

inline auto yield(uint64_t count = kj::maxValue) {
  return [count]() mutable -> kj::Promise<void> {
    if (count == 0) return KJ_EXCEPTION(OVERLOADED);
    --count;
    return kj::evalLast([]{});
  };
}

inline auto periodic(
  kj::Timer& timer,
  kj::Duration period = kj::MILLISECONDS,
  uint64_t count = kj::maxValue) {

  return [&timer, period, count]() mutable -> kj::Promise<void> {
    if (count == 0) return KJ_EXCEPTION(OVERLOADED);
    --count;
    return timer.afterDelay(period);
  };
}

}

}
