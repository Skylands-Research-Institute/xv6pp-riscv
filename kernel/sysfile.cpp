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
  kernel.cpus.curproc()->set_ofile(fd, nullptr);
  f->close();
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
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, newp, MAXPATH) < 0)
    return -1;

  kernel.log.begin_op();
  if ((ip = kernel.fsystem.namei(old)) == 0) {
    kernel.log.end_op();
    return -1;
  }

  kernel.fsystem.ilock(ip);
  if (ip->type == T_DIR) {
    kernel.fsystem.iunlockput(ip);
    kernel.log.end_op();
    return -1;
  }

  ip->nlink++;
  kernel.fsystem.iupdate(ip);
  kernel.fsystem.iunlock(ip);

  if ((dp = kernel.fsystem.nameiparent(newp, name)) == 0)
    goto bad;
  kernel.fsystem.ilock(dp);
  if (dp->dev != ip->dev || kernel.fsystem.dirlink(dp, name, ip->inum) < 0) {
    kernel.fsystem.iunlockput(dp);
    goto bad;
  }
  kernel.fsystem.iunlockput(dp);
  kernel.fsystem.iput(ip);

  kernel.log.end_op();

  return 0;

  bad: kernel.fsystem.ilock(ip);
  ip->nlink--;
  kernel.fsystem.iupdate(ip);
  kernel.fsystem.iunlockput(ip);
  kernel.log.end_op();
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
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  kernel.log.begin_op();
  if ((dp = kernel.fsystem.nameiparent(path, name)) == 0) {
    kernel.log.end_op();
    return -1;
  }

  kernel.fsystem.ilock(dp);

  // Cannot unlink "." or "..".
  if (kernel.fsystem.namecmp(name, ".") == 0 || kernel.fsystem.namecmp(name, "..") == 0)
    goto bad;

  if ((ip = kernel.fsystem.dirlookup(dp, name, &off)) == 0)
    goto bad;
  kernel.fsystem.ilock(ip);

  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip)) {
    kernel.fsystem.iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (kernel.fsystem.writei(dp, 0, (uint64) &de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR) {
    dp->nlink--;
    kernel.fsystem.iupdate(dp);
  }
  kernel.fsystem.iunlockput(dp);

  ip->nlink--;
  kernel.fsystem.iupdate(ip);
  kernel.fsystem.iunlockput(ip);

  kernel.log.end_op();

  return 0;

  bad: kernel.fsystem.iunlockput(dp);
  kernel.log.end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor) {
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = kernel.fsystem.nameiparent(path, name)) == 0)
    return 0;

  kernel.fsystem.ilock(dp);

  if ((ip = kernel.fsystem.dirlookup(dp, name, 0)) != 0) {
    kernel.fsystem.iunlockput(dp);
    kernel.fsystem.ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    kernel.fsystem.iunlockput(ip);
    return 0;
  }

  if ((ip = kernel.fsystem.ialloc(dp->dev, type)) == 0) {
    kernel.fsystem.iunlockput(dp);
    return 0;
  }

  kernel.fsystem.ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  kernel.fsystem.iupdate(ip);

  if (type == T_DIR) {  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (kernel.fsystem.dirlink(ip, (char*) ".", ip->inum) < 0 || kernel.fsystem.dirlink(ip, (char*) "..", dp->inum) < 0)
      goto fail;
  }

  if (kernel.fsystem.dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if (type == T_DIR) {
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    kernel.fsystem.iupdate(dp);
  }

  kernel.fsystem.iunlockput(dp);

  return ip;

  fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  kernel.fsystem.iupdate(ip);
  kernel.fsystem.iunlockput(ip);
  kernel.fsystem.iunlockput(dp);
  return 0;
}

uint64 sys_open(void) {
  char path[MAXPATH];
  int fd, omode;
  file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if ((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  kernel.log.begin_op();

  if (omode & O_CREATE) {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0) {
      kernel.log.end_op();
      return -1;
    }
  } else {
    if ((ip = kernel.fsystem.namei(path)) == 0) {
      kernel.log.end_op();
      return -1;
    }
    kernel.fsystem.ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY) {
      kernel.fsystem.iunlockput(ip);
      kernel.log.end_op();
      return -1;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
    kernel.fsystem.iunlockput(ip);
    kernel.log.end_op();
    return -1;
  }

  if ((f = kernel.fmanager.alloc_file()) == 0 || (fd = fdalloc(f)) < 0) {
    if (f)
      f->close();
    kernel.fsystem.iunlockput(ip);
    kernel.log.end_op();
    return -1;
  }

  if (ip->type == T_DEVICE) {
    f->set_type(file::FD_DEVICE);
    f->set_major(ip->major);
  } else {
    f->set_type(file::FD_INODE);
    f->set_off(0);
  }
  f->set_ip(ip);
  f->set_readable(!(omode & O_WRONLY));
  f->set_writable((omode & O_WRONLY) || (omode & O_RDWR));

  if ((omode & O_TRUNC) && ip->type == T_FILE) {
    kernel.fsystem.itrunc(ip);
  }

  kernel.fsystem.iunlock(ip);
  kernel.log.end_op();

  return fd;
}

uint64 sys_mkdir(void) {
  char path[MAXPATH];
  struct inode *ip;

  kernel.log.begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0) {
    kernel.log.end_op();
    return -1;
  }
  kernel.fsystem.iunlockput(ip);
  kernel.log.end_op();
  return 0;
}

uint64 sys_mknod(void) {
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  kernel.log.begin_op();
  argint(1, &major);
  argint(2, &minor);
  if ((argstr(0, path, MAXPATH)) < 0 || (ip = create(path, T_DEVICE, major, minor)) == 0) {
    kernel.log.end_op();
    return -1;
  }
  kernel.fsystem.iunlockput(ip);
  kernel.log.end_op();
  return 0;
}

uint64 sys_chdir(void) {
  char path[MAXPATH];
  struct inode *ip;
  process *p = kernel.cpus.curproc();

  kernel.log.begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = kernel.fsystem.namei(path)) == 0) {
    kernel.log.end_op();
    return -1;
  }
  kernel.fsystem.ilock(ip);
  if (ip->type != T_DIR) {
    kernel.fsystem.iunlockput(ip);
    kernel.log.end_op();
    return -1;
  }
  kernel.fsystem.iunlock(ip);
  kernel.fsystem.iput(p->get_cwd());
  kernel.log.end_op();
  p->set_cwd(ip);
  return 0;
}

uint64 sys_exec(void) {
  char path[MAXPATH], *argv[MAXARG];
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
      argv[i] = (char*) kernel.allocator.alloc();
      if (argv[i] == 0)
        bad = true;
      else if (fetchstr(uarg, argv[i], PGSIZE) < 0)
        bad = true;
    }
  }

  if (bad) {
    for (i = 0; i < (int) NELEM(argv) && argv[i] != 0; i++)
      kernel.allocator.free(argv[i]);
    return -1;
  }

  //int ret = exec(path, argv);
  int ret = kernel.cpus.curproc()->exec(path, argv);

  for (i = 0; i < (int) NELEM(argv) && argv[i] != 0; i++)
    kernel.allocator.free(argv[i]);

  return ret;

}

uint64 sys_pipe(void) {
  uint64 fdarray; // user pointer to array of two integers
  file *rf, *wf;
  int fd0, fd1;
  process *p = kernel.cpus.curproc();

  argaddr(0, &fdarray);
  if (kernel.fmanager.alloc_pipe(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
    if (fd0 >= 0)
      p->set_ofile(fd0, nullptr);
    rf->close();
    wf->close();
    return -1;
  }
  if (kernel.memory.copyout(p->get_pagetable(), fdarray, (char*) &fd0, sizeof(fd0)) < 0
      || kernel.memory.copyout(p->get_pagetable(), fdarray + sizeof(fd0), (char*) &fd1, sizeof(fd1)) < 0) {
    p->set_ofile(fd0, nullptr);
    p->set_ofile(fd1, nullptr);
    rf->close();
    wf->close();
    return -1;
  }
  return 0;
}
