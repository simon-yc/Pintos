#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <list.h>

/* Process identifier. */
typedef int pid_t;
struct lock filesys_lock;
struct opened_file {
    int fd;
    struct file *file;
    struct list_elem file_elem;
};

/* P2 update - syscall handlers and helper functions */
void syscall_init (void);
void handle_halt (void);
void handle_exit (int);
pid_t handle_exec (const char *);
bool handle_create (const char *, unsigned);
bool handle_remove (const char *);
int handle_open (const char *);
int handle_filesize (int);
int handle_read (int, void *, unsigned);
int handle_write (int, void *, unsigned);
void handle_seek (int, unsigned);
unsigned handle_tell (int);
void handle_close (int);

#endif /* userprog/syscall.h */
