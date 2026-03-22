//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "fs.h"
#include "fcntl.h"
#include "xv6pp.h"
#include "file_manager.h"
#include "file_ref_guard.h"
#include "inode_lock_guard.h"
#include "inode_ref_guard.h"
#include "log_op_guard.h"
#include "page_ref_guard.h"
#include "syscall_args.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int argfd(int n, int *pfd, file **pf) {
  int fd;
  file *f;

  argint(n, &fd);
  if (fd < 0 || fd >= NOFILE || (f = kernel.cpus.curproc()->get_ofile(fd)) == nullptr)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
int fdalloc(file *f) {
  int fd;
  process *p = kernel.cpus.curproc();

  for (fd = 0; fd < NOFILE; fd++) {
    if (p->get_ofile(fd) == 0) {
      p->set_ofile(fd, f);
      return fd;
    }
  }
  return -1;
}

uint64 sys_dup(void) {
  file *f;
  int fd;

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
    return -1;
  f->dup();
  return fd;
}

uint64 sys_read(void) {
  file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return f->read(p, n);
}

uint64 sys_write(void) {
  file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return f->write(p, n);
}

uint64 sys_close(void) {
  int fd;
  file *f;

  if (argfd(0, &fd, &f) < 0)
    return -1;
  file_ref_guard f_ref(f);
  kernel.cpus.curproc()->set_ofile(fd, nullptr);
  return 0;
}

uint64 sys_fstat(void) {
  file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return f->stat(st);
}

// Create the path new as a link to the same inode as old.
uint64 sys_link(void) {
  char name[DIRSIZ], newp[MAXPATH], old[MAXPATH];

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, newp, MAXPATH) < 0)
    return -1;

  log_op_guard log_guard;
  inode_ref_guard ip_ref(kernel.fsystem.namei(old));
  if (!ip_ref) {
    return -1;
  }

  inode_lock_guard ip_lock(ip_ref.get());
  if (ip_ref.get()->type == T_DIR) {
    return -1;
  }

  ip_ref.get()->nlink++;
  kernel.fsystem.iupdate(ip_ref.get());
  ip_lock.unlock();

  inode_ref_guard dp_ref(kernel.fsystem.nameiparent(newp, name));
  if (dp_ref) {
    inode_lock_guard dp_lock(dp_ref.get());
    if (dp_ref.get()->dev == ip_ref.get()->dev && kernel.fsystem.dirlink(dp_ref.get(), name, ip_ref.get()->inum) >= 0) {
      return 0;
    }
  }

  ip_lock.reset(ip_ref.get());
  ip_ref.get()->nlink--;
  kernel.fsystem.iupdate(ip_ref.get());
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(struct inode *dp) {
  struct dirent de;

  for (auto off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
    if (kernel.fsystem.readi(dp, 0, (uint64) &de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

uint64 sys_unlink(void) {
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  log_op_guard log_guard;
  inode_ref_guard dp_ref(kernel.fsystem.nameiparent(path, name));
  if (!dp_ref) {
    return -1;
  }

  inode_lock_guard dp_lock(dp_ref.get());

  // Cannot unlink "." or "..".
  if (kernel.fsystem.namecmp(name, ".") == 0 || kernel.fsystem.namecmp(name, "..") == 0)
    return -1;

  inode_ref_guard ip_ref(kernel.fsystem.dirlookup(dp_ref.get(), name, &off));
  if (!ip_ref)
    return -1;

  inode_lock_guard ip_lock(ip_ref.get());
  if (ip_ref.get()->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip_ref.get()->type == T_DIR && !isdirempty(ip_ref.get()))
    return -1;

  memset(&de, 0, sizeof(de));
  if (kernel.fsystem.writei(dp_ref.get(), 0, (uint64) &de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip_ref.get()->type == T_DIR) {
    dp_ref.get()->nlink--;
    kernel.fsystem.iupdate(dp_ref.get());
  }
  dp_lock.unlock();
  dp_ref.reset();

  ip_ref.get()->nlink--;
  kernel.fsystem.iupdate(ip_ref.get());

  return 0;
}

static struct inode*
create(char *path, short type, short major, short minor) {
  char name[DIRSIZ];

  inode_ref_guard dp_ref(kernel.fsystem.nameiparent(path, name));
  if (!dp_ref)
    return 0;

  inode_lock_guard dp_lock(dp_ref.get());

  inode_ref_guard ip_ref(kernel.fsystem.dirlookup(dp_ref.get(), name, 0));
  if (ip_ref) {
    dp_lock.unlock();
    dp_ref.reset();
    inode_lock_guard ip_lock(ip_ref.get());
    if (type == T_FILE && (ip_ref.get()->type == T_FILE || ip_ref.get()->type == T_DEVICE)) {
      ip_lock.release();
      return ip_ref.release();
    }
    return 0;
  }

  ip_ref.reset(kernel.fsystem.ialloc(dp_ref.get()->dev, type));
  if (!ip_ref) {
    return 0;
  }

  inode_lock_guard ip_lock(ip_ref.get());
  ip_ref.get()->major = major;
  ip_ref.get()->minor = minor;
  ip_ref.get()->nlink = 1;
  kernel.fsystem.iupdate(ip_ref.get());

  if (type == T_DIR) {  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (kernel.fsystem.dirlink(ip_ref.get(), (char*) ".", ip_ref.get()->inum) < 0
        || kernel.fsystem.dirlink(ip_ref.get(), (char*) "..", dp_ref.get()->inum) < 0)
      goto fail;
  }

  if (kernel.fsystem.dirlink(dp_ref.get(), name, ip_ref.get()->inum) < 0)
    goto fail;

  if (type == T_DIR) {
    // now that success is guaranteed:
    dp_ref.get()->nlink++;  // for ".."
    kernel.fsystem.iupdate(dp_ref.get());
  }

  dp_lock.unlock();
  dp_ref.reset();
  ip_lock.release();
  return ip_ref.release();

  fail:
  // something went wrong. de-allocate ip.
  ip_ref.get()->nlink = 0;
  kernel.fsystem.iupdate(ip_ref.get());
  return 0;
}

uint64 sys_open(void) {
  char path[MAXPATH];
  int fd, omode;
  int n;

  argint(1, &omode);
  if ((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  log_op_guard log_guard;
  inode_ref_guard ip_ref;
  inode_lock_guard ip_lock;

  if (omode & O_CREATE) {
    ip_ref.reset(create(path, T_FILE, 0, 0));
    if (!ip_ref) {
      return -1;
    }
    ip_lock.adopt(ip_ref.get());
  } else {
    ip_ref.reset(kernel.fsystem.namei(path));
    if (!ip_ref) {
      return -1;
    }
    ip_lock.reset(ip_ref.get());
    if (ip_ref.get()->type == T_DIR && omode != O_RDONLY) {
      return -1;
    }
  }

  if (ip_ref.get()->type == T_DEVICE && (ip_ref.get()->major < 0 || ip_ref.get()->major >= NDEV)) {
    return -1;
  }

  file_ref_guard f_ref(kernel.fmanager.alloc_file());
  if (!f_ref || (fd = fdalloc(f_ref.get())) < 0) {
    return -1;
  }

  if (ip_ref.get()->type == T_DEVICE) {
    f_ref.get()->set_type(file::FD_DEVICE);
    f_ref.get()->set_major(ip_ref.get()->major);
  } else {
    f_ref.get()->set_type(file::FD_INODE);
    f_ref.get()->set_off(0);
  }
  f_ref.get()->set_ip(ip_ref.get());
  f_ref.get()->set_readable(!(omode & O_WRONLY));
  f_ref.get()->set_writable((omode & O_WRONLY) || (omode & O_RDWR));

  if ((omode & O_TRUNC) && ip_ref.get()->type == T_FILE) {
    kernel.fsystem.itrunc(ip_ref.get());
  }

  ip_lock.unlock();
  ip_ref.release();
  f_ref.release();

  return fd;
}

uint64 sys_mkdir(void) {
  char path[MAXPATH];

  log_op_guard log_guard;
  if (argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  inode_ref_guard ip_ref(create(path, T_DIR, 0, 0));
  if (!ip_ref) {
    return -1;
  }
  inode_lock_guard ip_lock;
  ip_lock.adopt(ip_ref.get());
  return 0;
}

uint64 sys_mknod(void) {
  char path[MAXPATH];
  int major, minor;

  log_op_guard log_guard;
  argint(1, &major);
  argint(2, &minor);
  if ((argstr(0, path, MAXPATH)) < 0) {
    return -1;
  }
  inode_ref_guard ip_ref(create(path, T_DEVICE, major, minor));
  if (!ip_ref) {
    return -1;
  }
  inode_lock_guard ip_lock;
  ip_lock.adopt(ip_ref.get());
  return 0;
}

uint64 sys_chdir(void) {
  char path[MAXPATH];
  process *p = kernel.cpus.curproc();

  log_op_guard log_guard;
  if (argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  inode_ref_guard ip_ref(kernel.fsystem.namei(path));
  if (!ip_ref) {
    return -1;
  }
  inode_lock_guard ip_lock(ip_ref.get());
  if (ip_ref.get()->type != T_DIR) {
    return -1;
  }
  ip_lock.unlock();
  inode_ref_guard cwd_ref(p->get_cwd());
  p->set_cwd(ip_ref.release());
  return 0;
}

uint64 sys_exec(void) {
  char path[MAXPATH], *argv[MAXARG];
  page_ref_guard arg_guards[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if (argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  bool bad = false;
  for (i = 0; !bad; i++) {
    if (i >= (int) NELEM(argv)) {
      bad = true;
    } else if (fetchaddr(uargv + sizeof(uint64) * i, (uint64*) &uarg) < 0) {
      bad = true;
    } else if (uarg == 0) {
      argv[i] = 0;
      break;
    } else {
      arg_guards[i].reset(kernel.allocator.alloc());
      argv[i] = (char*) arg_guards[i].get();
      if (argv[i] == 0)
        bad = true;
      else if (fetchstr(uarg, argv[i], PGSIZE) < 0)
        bad = true;
    }
  }

  if (bad)
    return -1;

  //int ret = exec(path, argv);
  int ret = kernel.cpus.curproc()->exec(path, argv);

  return ret;

}

uint64 sys_pipe(void) {
  uint64 fdarray; // user pointer to array of two integers
  int fd0, fd1;
  process *p = kernel.cpus.curproc();

  argaddr(0, &fdarray);
  file *rf = nullptr, *wf = nullptr;
  if (kernel.fmanager.alloc_pipe(&rf, &wf) < 0)
    return -1;
  file_ref_guard rf_ref(rf);
  file_ref_guard wf_ref(wf);
  fd0 = -1;
  if ((fd0 = fdalloc(rf_ref.get())) < 0 || (fd1 = fdalloc(wf_ref.get())) < 0) {
    if (fd0 >= 0)
      p->set_ofile(fd0, nullptr);
    return -1;
  }
  if (kernel.memory.copyout(p->get_pagetable(), fdarray, (char*) &fd0, sizeof(fd0)) < 0
      || kernel.memory.copyout(p->get_pagetable(), fdarray + sizeof(fd0), (char*) &fd1, sizeof(fd1)) < 0) {
    p->set_ofile(fd0, nullptr);
    p->set_ofile(fd1, nullptr);
    return -1;
  }
  rf_ref.release();
  wf_ref.release();
  return 0;
}
