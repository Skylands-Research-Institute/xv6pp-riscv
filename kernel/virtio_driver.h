#pragma once

#include "types.h"
#include "spin_lock.h"
#include "virtio.h"
#include "block_device.h"

struct buf;

class virtio_driver final : public block_device_driver {
private:
  // a set (not a ring) of DMA descriptors, with which the
  // driver tells the device where to read and write individual
  // disk operations. there are NUM descriptors.
  // most commands consist of a "chain" (a linked list) of a couple of
  // these descriptors.
  struct virtq_desc *desc;

  // a ring in which the driver writes descriptor numbers
  // that the driver would like the device to process.  it only
  // includes the head descriptor of each chain. the ring has
  // NUM elements.
  struct virtq_avail *avail;

  // a ring in which the device writes descriptor numbers that
  // the device has finished processing (just the head of each chain).
  // there are NUM used ring entries.
  struct virtq_used *used;

  // our own book-keeping.
  char free[NUM];    // is a descriptor free?
  uint16 used_idx;    // we've looked this far in used[2..NUM].

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  struct {
    struct buf *b;
    char status;
  } info[NUM];

  // disk command headers.
  // one-for-one with descriptors, for convenience.
  struct virtio_blk_req ops[NUM];

  spin_lock vdisk_lock;

  int alloc_desc();
  void free_desc(int desci);
  void free_chain(int desci);
  int alloc3_desc(int *idx);
  void read_write(buf *b, bool write);

public:
  explicit virtio_driver(const char *name);

  void init() override;
  void read(buf *b);
  void write(buf *b);
  void handle_interrupt();
};


