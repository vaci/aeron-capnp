#pragma once

#include <kj/debug.h>
#include <kj/list.h>

namespace kj {

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
