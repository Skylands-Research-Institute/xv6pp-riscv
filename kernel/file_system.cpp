#include "types.h"
#include "stat.h"
#include "file_system.h"
#include "buffer_guard.h"
#include "lock_guard.h"
#include "file_system_log.h"
#include "xv6pp.h"

file_system::file_system(const char *name) :
    kernel_module(name), sb(), inode_lock(name) {
}

void file_system::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
}

void file_system::readsb(int dev) {
  buffer_guard bp(dev, 1);
  memmove(&sb, bp->data, sizeof(sb));
}

void file_system::bzero(int dev, int bno) {
  buffer_guard bp(dev, bno);
  memset(bp->data, 0, BSIZE);
  kernel.log.log_write(bp);
}

uint file_system::balloc(uint dev) {
  for (uint b = 0; b < sb.size; b += BPB) {
    buffer_guard bp(dev, BBLOCK(b, sb));
    for (uint bi = 0; bi < BPB && b + bi < sb.size; bi++) {
      uint m = 1 << (bi % 8);
      if ((bp->data[bi / 8] & m) == 0) {  // Is block free?
        bp->data[bi / 8] |= m;  // Mark block in use.
        kernel.log.log_write(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
  }
  log(log_level::WARN, "balloc: out of blocks\n");
  return 0;
}

void file_system::bfree(int dev, uint b) {
  buffer_guard bp(dev, BBLOCK(b, sb));
  uint bi = b % BPB;
  uint m = 1 << (bi % 8);
  if ((bp->data[bi / 8] & m) == 0)
    panic("freeing free block");
  bp->data[bi / 8] &= ~m;
  kernel.log.log_write(bp);
}

struct inode* file_system::iget(uint dev, uint inum) {
  lock_guard<spin_lock> g(inode_lock);
  // Is the inode already in the table?
  struct inode *empty = 0;
  for (struct inode *ip = &inodes[0]; ip < &inodes[NINODE]; ip++) {
    if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
      ip->ref++;
      return ip;
    }
    if (empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }
  // Recycle an inode entry.
  if (!empty)
    panic("iget: no inodes");
  struct inode *ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  return ip;
}

void file_system::fsinit(int dev) {
  readsb(dev);
  if (sb.magic != FSMAGIC)
    panic("invalid file system");
  kernel.log.init(dev, &sb);
}

int file_system::dirlink(struct inode *dp, char *name, uint inum) {
  struct inode *ip = dirlookup(dp, name, 0);
  // Check that name is not present.
  if (ip) {
    iput(ip);
    return -1;
  }
  struct dirent de;
  // Look for an empty dirent.
  uint off;
  for (off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64) &de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if (!de.inum)
      break;
  }
  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if (writei(dp, 0, (uint64) &de, off, sizeof(de)) != sizeof(de))
    return -1;
  return 0;
}

struct inode* file_system::dirlookup(struct inode *dp, char *name, uint *poff) {
  if (dp->type != T_DIR)
    panic("dirlookup not DIR");
  struct dirent de;
  for (uint off = 0; off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64) &de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if (de.inum == 0)
      continue;
    if (namecmp(name, de.name) == 0) {
      // entry matches path element
      if (poff)
        *poff = off;
      auto inum = de.inum;
      return iget(dp->dev, inum);
    }
  }
  return nullptr;
}

struct inode* file_system::ialloc(uint dev, short type) {
  for (uint inum = 1; inum < sb.ninodes; inum++) {
    buffer_guard bp(dev, IBLOCK(inum, sb));
    struct dinode *dip = (struct dinode*) bp->data + inum % IPB;
    if (dip->type == 0) {  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      kernel.log.log_write(bp);   // mark it allocated on the disk
      return iget(dev, inum);
    }
  }
  log(log_level::WARN, "ialloc: no inodes\n");
  return nullptr;
}

struct inode* file_system::idup(struct inode *ip) {
  lock_guard<spin_lock> g(inode_lock);
  ip->ref++;
  return ip;
}

void file_system::ilock(struct inode *ip) {
  if (ip == 0 || ip->ref < 1)
    panic("ilock");
  ip->lock.acquire();
  if (ip->valid == 0) {
    buffer_guard bp(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode *dip = (struct dinode*) bp->data + ip->inum % IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    ip->valid = 1;
    if (ip->type == 0)
      panic("ilock: no type");
  }
}

void file_system::iput(struct inode *ip) {
  inode_lock.acquire();
  if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
    // inode has no links and no other references: truncate and free.
    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock).
    ip->lock.acquire();
    inode_lock.release();
    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;
    ip->lock.release();
    inode_lock.acquire();
  }
  ip->ref--;
  inode_lock.release();
}

void file_system::iunlock(struct inode *ip) {
  if (ip == 0 || !(ip->lock.holding() || ip->ref < 1))
    panic("iunlock");
  ip->lock.release();
}

void file_system::iunlockput(struct inode *ip) {
  iunlock(ip);
  iput(ip);
}

void file_system::iupdate(struct inode *ip) {
  buffer_guard bp(ip->dev, IBLOCK(ip->inum, sb));
  struct dinode *dip = (struct dinode*) bp->data + ip->inum % IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  kernel.log.log_write(bp);
}

int file_system::namecmp(const char *s, const char *t) {
  return strncmp(s, t, DIRSIZ);
}

char* file_system::skipelem(char *path, char *name) {
  char *s;
  int len;
  while (*path == '/')
    path++;
  if (*path == 0)
    return 0;
  s = path;
  while (*path != '/' && *path != 0)
    path++;
  len = path - s;
  if (len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while (*path == '/')
    path++;
  return path;
}

struct inode* file_system::namex(char *path, int nameiparent, char *name) {
  struct inode *ip;
  if (*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(kernel.cpus.curproc()->get_cwd());
  while ((path = skipelem(path, name)) != 0) {
    ilock(ip);
    if (ip->type != T_DIR) {
      iunlockput(ip);
      return 0;
    }
    if (nameiparent && *path == '\0') {
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    struct inode *next;
    if ((next = dirlookup(ip, name, 0)) == 0) {
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if (nameiparent) {
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode* file_system::namei(char *path) {
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode* file_system::nameiparent(char *path, char *name) {
  return namex(path, 1, name);
}

uint file_system::bmap(struct inode *ip, uint bn) {
  uint addr;
  if (bn < NDIRECT) {
    if ((addr = ip->addrs[bn]) == 0) {
      addr = balloc(ip->dev);
      if (addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;
  if (bn < NINDIRECT) {
    // Load indirect block, allocating if necessary.
    if ((addr = ip->addrs[NDIRECT]) == 0) {
      addr = balloc(ip->dev);
      if (addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    buffer_guard bp(ip->dev, addr);
    auto a = (uint*) bp->data;
    if ((addr = a[bn]) == 0) {
      addr = balloc(ip->dev);
      if (addr) {
        a[bn] = addr;
        kernel.log.log_write(bp);
      }
    }
    return addr;
  }
  panic("bmap: out of range");
}

static inline uint min(uint a, uint b) {
  return a < b ? a : b;
}

int file_system::readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n) {
  if (off > ip->size || off + n < off)
    return 0;
  if (off + n > ip->size)
    n = ip->size - off;
  uint m, tot;
  for (tot = 0; tot < n; tot += m, off += m, dst += m) {
    uint addr = bmap(ip, off / BSIZE);
    if (addr == 0)
      break;
    buffer_guard bp(ip->dev, addr);
    m = min(n - tot, BSIZE - off % BSIZE);
    if (kernel.memory.either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      tot = -1;
      break;
    }
  }
  return tot;
}

void file_system::stati(struct inode *ip, struct stat *st) {
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

int file_system::writei(struct inode *ip, int user_src, uint64 src, uint off, uint n) {
  if (off > ip->size || off + n < off)
    return -1;
  if (off + n > MAXFILE * BSIZE)
    return -1;
  uint tot, m;
  for (tot = 0; tot < n; tot += m, off += m, src += m) {
    uint addr = bmap(ip, off / BSIZE);
    if (addr == 0)
      break;
    buffer_guard bp(ip->dev, addr);
    m = min(n - tot, BSIZE - off % BSIZE);
    if (kernel.memory.either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      break;
    }
    kernel.log.log_write(bp);
  }
  if (off > ip->size)
    ip->size = off;
  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  iupdate(ip);
  return tot;
}

void file_system::itrunc(struct inode *ip) {
  for (auto i = 0; i < NDIRECT; i++) {
    if (ip->addrs[i]) {
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }
  if (ip->addrs[NDIRECT]) {
    auto bp = kernel.cache.read(ip->dev, ip->addrs[NDIRECT]);
    auto a = (uint*) bp->data;
    for (uint j = 0; j < NINDIRECT; j++) {
      if (a[j])
        bfree(ip->dev, a[j]);
    }
    kernel.cache.relse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }
  ip->size = 0;
  iupdate(ip);
}

