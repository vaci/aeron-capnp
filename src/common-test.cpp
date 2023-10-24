// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "common.h"
#include <kj/main.h>
#include <gtest/gtest.h>

TEST(Own, Basic) {

  auto foo = std::make_shared<int>(1);
  {
    auto bar = kj::attachPtr(foo);
  }
  EXPECT_EQ(foo.use_count(), 1);
}

int main(int argc, char* argv[]) {
  kj::TopLevelProcessContext processCtx{argv[0]};
  processCtx.increaseLoggingVerbosity();

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
