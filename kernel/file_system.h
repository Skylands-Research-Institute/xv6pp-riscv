#pragma once

#include "buffer_cache.h"
#include "kernel_module.h"
#include "sleep_lock.h"
#include "param.h"
#include "fs.h"

// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  sleep_lock lock; // protects everything below here
  int valid;          // inode has been read from disk?
  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT + 1];
};

class file_system final : public kernel_module {
private:
  // there should be one superblock per disk device, but we run with
  // only one device
  struct superblock sb;
  spin_lock inode_lock;
  struct inode inodes[NINODE];

  void readsb(int dev);
  void bzero(int dev, int bno);
  uint balloc(uint dev);
  struct inode* iget(uint dev, uint inum);
  struct inode* namex(char *path, int nameiparent, char *name);
  char* skipelem(char *path, char *name);
  uint bmap(struct inode *ip, uint bn);

public:
  explicit file_system(const char *name);
  void init() override;
  void fsinit(int dev);
  void bfree(int dev, uint b);
  int dirlink(struct inode*, char*, uint);
  struct inode* dirlookup(struct inode*, char*, uint*);
  struct inode* ialloc(uint, short);
  struct inode* idup(struct inode*);
  void ilock(struct inode*);
  void iput(struct inode*);
  void iunlock(struct inode*);
  void iunlockput(struct inode*);
  void iupdate(struct inode*);
  int namecmp(const char*, const char*);
  struct inode* namei(char*);
  struct inode* nameiparent(char*, char*);
  int readi(struct inode*, int, uint64, uint, uint);
  void stati(struct inode*, struct stat*);
  int writei(struct inode*, int, uint64, uint, uint);
  void itrunc(struct inode*);
};

