#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <list.h>

/* Process identifier. */
typedef int pid_t;
typedef int mapid_t;
struct lock filesys_lock;
struct opened_file {
    int fd;
    struct file *file;
    struct list_elem file_elem;
};

/* Binds a mapping id to a region of memory and a file. */
struct file_map
  {
    int fd;                     /* file's fd. */
    struct file *file;          
    uint8_t *vaddr;             /* User virtual address. */
    int page_num;               /* total number of pages for this file_map. */
    struct list_elem elem;      /* file_map's list elem */
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
mapid_t handle_mmap (int, void *);
void handle_munmap (mapid_t);
void files_exit(void);
#endif /* userprog/syscall.h */
