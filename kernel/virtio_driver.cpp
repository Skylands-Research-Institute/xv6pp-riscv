#include "virtio_driver.h"
#include "page_allocator.h"
#include "lock_guard.h"
#include "sleep_lock.h"
#include "buffer_cache.h"
#include "xv6pp.h"

// the address of virtio mmio register r.
static inline volatile unsigned int* R(unsigned int r) {
  return reinterpret_cast<volatile unsigned int*>(static_cast<unsigned long>(VIRTIO0)
      + r);
}

virtio_driver::virtio_driver(const char *name) :
    block_device_driver(name), vdisk_lock(name) {
}

void virtio_driver::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));

  // Initialize virtio queue, descriptors, interrupt registration
  uint32 status = 0;

  if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 || *R(VIRTIO_MMIO_VERSION) != 2
      || *R(VIRTIO_MMIO_DEVICE_ID) != 2
      || *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
    panic("could not find virtio disk");
  }

  // reset device
  *R(VIRTIO_MMIO_STATUS) = status;

  // set ACKNOWLEDGE status bit
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // set DRIVER status bit
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // negotiate features
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // re-read status to ensure FEATURES_OK is set.
  status = *R(VIRTIO_MMIO_STATUS);
  if (!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // initialize queue 0.
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  // ensure queue 0 is not in use.
  if (*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // check maximum queue size.
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0)
    panic("virtio disk has no queue 0");
  if (max < NUM)
    panic("virtio disk max queue too short");

  // allocate and zero queue memory.
  desc = (virtq_desc*) kernel.allocator.alloc();
  avail = (virtq_avail*) kernel.allocator.alloc();
  used = (virtq_used*) kernel.allocator.alloc();
  if (!desc || !avail || !used)
    panic("virtio disk kalloc");
  memset(desc, 0, PGSIZE);
  memset(avail, 0, PGSIZE);
  memset(used, 0, PGSIZE);

  // set queue size.
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // write physical addresses.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64) desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64) desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64) avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64) avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64) used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64) used >> 32;

  // queue is ready.
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // all NUM descriptors start out unused.
  for (int i = 0; i < NUM; i++)
    free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

int virtio_driver::alloc_desc() {
  int desci = -1;
  for (int i = 0; i < NUM && desci == -1; i++) {
    if (free[i]) {
      free[i] = 0;
      desci = i;
    }
  }
  return desci;
}

void virtio_driver::free_desc(int desci) {
  if (desci >= NUM)
    panic("free_desc 1");
  if (free[desci])
    panic("free_desc 2");
  desc[desci].addr = 0;
  desc[desci].len = 0;
  desc[desci].flags = 0;
  desc[desci].next = 0;
  free[desci] = 1;
  kernel.processes.wakeup(&free[0]);
}

void virtio_driver::free_chain(int desci) {
  while (1) {
    int flag = desc[desci].flags;
    int nxt = desc[desci].next;
    free_desc(desci);
    if (flag & VRING_DESC_F_NEXT)
      desci = nxt;
    else
      break;
  }
}

int virtio_driver::alloc3_desc(int *idx) {
  for (int i = 0; i < 3; i++) {
    idx[i] = alloc_desc();
    if (idx[i] < 0) {
      for (int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

void virtio_driver::read_write(buf *b, bool write) {
  lock_guard < spin_lock > g(vdisk_lock);

  uint64 sector = b->blockno * (BSIZE / 512);

  // the spec's Section 5.2 says that legacy block operations use
  // three descriptors: one for type/reserved/sector, one for the
  // data, one for a 1-byte status result.

  // allocate the three descriptors.
  int idx[3];
  while (1) {
    if (alloc3_desc(idx) == 0) {
      break;
    }
    kernel.interrupts.sleep(&free[0], vdisk_lock);
  }

  // format the three descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_blk_req *buf0 = &ops[idx[0]];

  if (write)
    buf0->type = VIRTIO_BLK_T_OUT; // write the disk
  else
    buf0->type = VIRTIO_BLK_T_IN; // read the disk

  buf0->reserved = 0;
  buf0->sector = sector;

  desc[idx[0]].addr = (uint64) buf0;
  desc[idx[0]].len = sizeof(struct virtio_blk_req);
  desc[idx[0]].flags = VRING_DESC_F_NEXT;
  desc[idx[0]].next = idx[1];

  desc[idx[1]].addr = (uint64) b->data;
  desc[idx[1]].len = BSIZE;

  if (write)
    desc[idx[1]].flags = 0; // device reads b->data
  else
    desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data

  desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  desc[idx[1]].next = idx[2];

  info[idx[0]].status = 0xff; // device writes 0 on success
  desc[idx[2]].addr = (uint64) &info[idx[0]].status;
  desc[idx[2]].len = 1;
  desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
  desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  b->disk = 1;
  info[idx[0]].b = b;

  // tell the device the first index in our chain of descriptors.
  avail->ring[avail->idx % NUM] = idx[0];

  __sync_synchronize();

  // tell the device another avail ring entry is available.
  avail->idx += 1; // not % NUM ...

  __sync_synchronize();

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

  // Wait for virtio_disk_intr() to say request has finished.
  while (b->disk == 1) {
    kernel.interrupts.sleep(b, vdisk_lock);
  }

  info[idx[0]].b = 0;
  free_chain(idx[0]);
}

void virtio_driver::read(buf *b) {
  // Queue a read request for block b->blockno from device b->dev
  read_write(b, false);
}

void virtio_driver::write(buf *b) {
  // Queue a write request for block b->blockno from buffer b->data
  read_write(b, true);
}

void virtio_driver::handle_interrupt() {
  // Handle virtio completion interrupt
  lock_guard < spin_lock > g(vdisk_lock);

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // the device increments disk.used->idx when it
  // adds an entry to the used ring.

  while (used_idx != used->idx) {
    __sync_synchronize();
    int id = used->ring[used_idx % NUM].id;

    if (info[id].status != 0)
      panic("virtio_disk_intr status");

    struct buf *b = info[id].b;
    b->disk = 0;   // disk is done with buf
    kernel.processes.wakeup(b);

    used_idx += 1;
  }

}

