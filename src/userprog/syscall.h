#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <list.h>

/* Process identifier. */
typedef int pid_t;

struct opened_file {
    int fd;
    struct file *file;
    struct list_elem file_elem;
};


void syscall_init (void);

struct lock filesys_lock;

#endif /* userprog/syscall.h */
