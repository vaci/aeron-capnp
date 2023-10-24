// Copyright (c) 2023 Vaci Koblizek.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "queue.h"

#include <kj/debug.h>
#include <kj/main.h>

#include <gtest/gtest.h>

TEST(Queue, Basic) {

  kj::Queue<char> queue;
  queue.push('a');
  queue.push('b');
  queue.push('c');

  {
    auto ch = queue.pop();
    EXPECT_EQ(ch, 'a');
  }
  {
    auto ch = queue.pop();
    EXPECT_EQ(ch, 'b');
  }
  {
    auto ch = queue.pop();
    EXPECT_EQ(ch, 'c');
  }
}

TEST(Queue, MoveOnly) {
  kj::Queue<kj::Own<char>> queue;
  EXPECT_TRUE(queue.empty());

  queue.push(kj::heap<char>('a'));
  queue.push(kj::heap<char>('b'));
  queue.push(kj::heap<char>('c'));

  EXPECT_EQ(queue.size(), 3);
  EXPECT_FALSE(queue.empty());

  {
    auto ch = queue.pop();
    EXPECT_EQ(*ch, 'a');  
  }
  {
    auto ch = queue.pop();
    EXPECT_EQ(*ch, 'b');
  }
  {
    auto ch = queue.pop();
    EXPECT_EQ(*ch, 'c');
  }
  
  EXPECT_EQ(queue.size(), 0);
  EXPECT_TRUE(queue.empty());
}

int main(int argc, char* argv[]) {
  kj::TopLevelProcessContext processCtx{argv[0]};
  processCtx.increaseLoggingVerbosity();

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
