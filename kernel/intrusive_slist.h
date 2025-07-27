#pragma once

#include "types.h"

template<typename T>
class intrusive_slist {
private:
  T *head = nullptr;
  uint64 count = 0;

public:
  intrusive_slist() = default;

  intrusive_slist(const intrusive_slist&) = delete;
  intrusive_slist& operator=(const intrusive_slist&) = delete;

  bool empty() const {
    return head == nullptr;
  }

  void push(T *ptr) {
    ptr->next = head;  // requires T to have a `T* next` member
    head = ptr;
    count++;
  }

  T* pop() {
    if (!head)
      return nullptr;
    T *n = head;
    head = head->next;
    count--;
    return n;
  }

  T* peek() const {
    return head;
  }

  uint64 get_count() const {
    return count;
  }
};

