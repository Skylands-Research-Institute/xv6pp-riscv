#include "buffer_cache.h"
#include "lock_guard.h"
#include "sleep_lock.h"
#include "virtio_driver.h"
#include "block_device.h"
#include "xv6pp.h"

buffer_cache::buffer_cache(const char *name) :
    kernel_module(name) {
  for (int i = 0; i < NBUF; i++) {
    cache.push_back(&storage[i]);
  }
}

void buffer_cache::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
}

buf* buffer_cache::read(uint device, uint block) {
  buf *b;
  {
    lock_guard g(lock);
    // search for a matching buffer
    b = cache.front();
    while (b != nullptr && !(b->dev == device && b->blockno == block))
      b = b->next;
    if (b == nullptr) {
      // not cached, allocate using LRU free buffer
      b = cache.back();
      while (b != nullptr && b->refcnt > 0)
        b = b->prev;
      if (b == nullptr)
        panic("buffer_cache::read: no buffers");
      b->dev = device;
      b->blockno = block;
      b->valid = 0;
    }
    b->refcnt++;
    cache.remove(b);
    cache.push_front(b);
  }
  b->lock.acquire();
  if (!b->valid) {
    kernel.disk.read(b);
    b->valid = 1;
  }
  return b;
}

void buffer_cache::relse(buf *b) {
  if (!b->lock.holding())
    panic("buffer_cache::relse: not holding lock");
  b->lock.release();
  lock_guard g(lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // Move to front of LRU list
    cache.remove(b);
    cache.push_back(b);
  }
}

void buffer_cache::write(buf *b) {
  if (!b->lock.holding())
    panic("buffer_cache::write");
  kernel.disk.write(b);
}

void buffer_cache::pin(buf *b) {
  lock_guard g(lock);
  b->refcnt++;
}

void buffer_cache::unpin(buf *b) {
  lock_guard g(lock);
  if (b->refcnt == 0)
    panic("buffer_cache::unpin: refcnt underflow");
  b->refcnt--;
}

