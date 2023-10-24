#pragma once
// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include <kj/exception.h>
#include <kj/memory.h>

namespace kj {

template <typename Ptr, typename... Attachments>
auto attachPtr(Ptr ptr, Attachments&&... attachments) {
  return kj::attachRef(*ptr, kj::mv(ptr), kj::fwd<Attachments>(attachments)...);
}

}

namespace aeroncap {

kj::Exception toException(int err);


}
