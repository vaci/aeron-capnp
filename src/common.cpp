// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "common.h"

#include <Aeron.h>
#include <kj/debug.h>

namespace aeroncap {

kj::Exception toException(int err)  {
  switch (err) {
  case ::aeron::MAX_POSITION_EXCEEDED:
    return KJ_EXCEPTION(DISCONNECTED, "Max position exceeded");

  case ::aeron::NOT_CONNECTED:
    return KJ_EXCEPTION(DISCONNECTED, "Not connected");

  case ::aeron::PUBLICATION_CLOSED:
    return KJ_EXCEPTION(DISCONNECTED, "Publication closed");

  default:
    return KJ_EXCEPTION(FAILED, "Unknown Aeron error");
  }
}

}
