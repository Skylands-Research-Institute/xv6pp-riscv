#pragma once

#include "types.h"

template<typename T>
class intrusive_dlist {
private:
  T *head = nullptr;
  T *tail = nullptr;
  uint64 count = 0;

public:
  intrusive_dlist() = default;

  intrusive_dlist(const intrusive_dlist&) = delete;
  intrusive_dlist& operator=(const intrusive_dlist&) = delete;

  bool empty() const {
    return head == nullptr;
  }

  uint64 get_count() const {
    return count;
  }

  T* front() const {
    return head;
  }

  T* back() const {
    return tail;
  }

  void push_front(T *node) {
    node->prev = nullptr;
    node->next = head;
    if (head)
      head->prev = node;
    else
      tail = node;
    head = node;
    count++;
  }

  void push_back(T *node) {
    node->next = nullptr;
    node->prev = tail;
    if (tail)
      tail->next = node;
    else
      head = node;
    tail = node;
    count++;
  }

  void remove(T *node) {
    if (node->prev)
      node->prev->next = node->next;
    else
      head = node->next;

    if (node->next)
      node->next->prev = node->prev;
    else
      tail = node->prev;

    node->prev = node->next = nullptr;
    count--;
  }

  T* pop_front() {
    if (!head)
      return nullptr;
    T *n = head;
    remove(n);
    return n;
  }

  T* pop_back() {
    if (!tail)
      return nullptr;
    T *n = tail;
    remove(n);
    return n;
  }
};

