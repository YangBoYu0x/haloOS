struct context;
struct spinlock;
struct buf;
struct superblock;
struct sleeplock;
struct file;
struct cpu;
struct proc;

// swtch.S
void swtch(struct context*, struct context*);

// spinlock.c
void lock(struct spinlock*);
void unlock(struct spinlock*);
void initlock(struct spinlock*,char*);
void push_off();
void pop_off();

// sleeplock.c
void sleep_lock(struct sleeplock*);
void sleep_unlock(struct sleeplock*);
void sleep_initlock(struct sleeplock*,char*);

// exec.c
int exec(char*,char**);

// proc.c
int get_tasks();
void user_init();
void run_target_task_num(int);
void run_os_task();
struct cpu* mycpu();
int cpuid();
struct proc* myproc();
int allocpid();

// file.c
void init_filecache();
struct file* filealloc();

// memlayout.c
void uart_putstr(char*);
void uart_putc(char);
void uartinit();
int uartgetc();
void uartinterrupt();
char* getCmd();
int getShellResult();

// plic.c
int plic_claim();
void plicinit();
void plicinithart();
void complate_irq(int);

// trap.c
void trapinit();


// fs.c
struct inode* iget(uint,uint);
struct buf* bread(uint,uint);
int readi(struct inode*,int,uint64,uint,uint);
struct inode* inodeByName(struct inode*,char*);

// print.c
void printf(char*, ...);
void print(char*);
void println(char*);
void printP(uint64);
void printinit();
void panic(char*);

// virt.c
void virtio_disk_init();
void virt_disk_rw(struct buf*,int);
void virtio_disk_isr();

// spaceswap.c
void* copyout(void*,void*,int);

// uitl.c
char* memset(void*,int,int);
void* memmove(void*,void*,int);
int strncmp(const char*,const char*,int);