#include "type.h"
#include "riscv.h"
#include "defs.h"
#include "virt.h"
#include "fs.h"
#include "spinlock.h"


#define R(addr) ((volatile uint32*)(VIRTIO_MMIO_BASE + (addr)))

struct blk{
    uint32 dev;
    uint32 blockno;
    struct spinlock blklock;
    int disk;
    unsigned char data[BSIZE];
};

static struct disk{
    char pages[2 * PGSIZE];

    struct virt_desc *desc;

    struct virt_avail *avail;

    struct virt_used *used;

    char free[NUM]; 

    struct{
        struct blk *b;
        char status;
    } info[NUM];

    uint16 used_idx;

    struct virt_blk_req ops[NUM];

    struct spinlock disklock;
} __attribute__((aligned(PGSIZE))) disk;


struct blk b[3];


void virtio_tester(int write) {
    if(!b[0].dev){
        println("buffer init...");
        
        b[0].dev = 1;
        b[0].blockno = 1;
        for(int i = 0;i < BSIZE; i++){
            b[0].data[i] = 0;
        }

        initlock(&(b[0].blklock),"blklock");
    }

    println("block read...");

    virt_disk_rw(&b[0],write);
        
}

// 找到一个空闲描述符，将其标记为非空闲，返回它的索引
static int alloc_desc() {
  for(int i = 0; i < NUM; i++){
    if(disk.free[i]){
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

//将描述符标记为空闲
static void free_desc(int i)
{
  if(i >= NUM){
    println("free_desc 1");
    return;
  }
  if(disk.free[i]){
    println("free_desc 2");
    return;
  }
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
}

//分配三个描述符（它们不必是连续的）。 磁盘传输总是使用三个描述符
static int alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++){
        free_desc(idx[j]);
      }
      return -1;
    }
  }
  return 0;
}

static void free_chain(int i) {
  while (1) {
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if (flag & VRING_DESC_F_NEXT){
      i = nxt;
    } else{
      break;
    }
  }
}

void virt_disk_rw(struct blk *b, int write) {
    // 指定写入的扇区
    uint64 sector = b->blockno * (BSIZE / 512);

    lock(&disk.disklock);
    int idx[3];
    while (1) {
        if (alloc3_desc(idx) == 0) {
            break;
        }
    }

    struct virt_blk_req *buf0 = &disk.ops[idx[0]];

    if (write){
        buf0->type = VIRTIO_BLK_T_OUT;
    } else{
        buf0->type = VIRTIO_BLK_T_IN;
    }
    buf0->reserved = 0;             // The reserved portion is used to pad the header to 16 bytes and move the 32-bit sector field to the correct place.
    buf0->sector = sector;          // specify the sector that we wanna modified.

    disk.desc[idx[0]].addr = (uint64) buf0;
    disk.desc[idx[0]].len = sizeof(struct virt_blk_req);
    disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
    disk.desc[idx[0]].next = idx[1];

    disk.desc[idx[1]].addr = (uint64)b->data & 0xffffffff;
    disk.desc[idx[1]].len = BSIZE;
    if (write){
        disk.desc[idx[1]].flags = 0; // 设备读取 b->data
    }else{
        disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // 设备写入 b->data
    }
    disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
    disk.desc[idx[1]].next = idx[2];

    disk.info[idx[0]].status = 0;
    disk.desc[idx[2]].addr = (uint64)&disk.info[idx[0]].status;
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // 设备写入状态
    disk.desc[idx[2]].next = 0;

    // 为 virtio_disk_intr() 记录结构 buf
    b->disk = 1;
    disk.info[idx[0]].b = b;

    __sync_synchronize();

    // 告诉设备我们的描述符链中的第一个索引
    disk.avail->ring[disk.avail->index % NUM] = idx[0];

    __sync_synchronize();

    //告诉设备另一个可用ring条目可用
    disk.avail->index += 1; // not % NUM ...

    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; //当我们将0写入queue_notify时，设备会立即启动
    while (b->disk == 1) {
    }

    disk.info[idx[0]].b = 0;
    free_chain(idx[0]);

    unlock(&disk.disklock);
}


void virtio_disk_init() {
  uint32 status = 0;

  initlock(&disk.disklock, "virtlock");

  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 1 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
    println("could not find virtio disk");
    return;
  }
  
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // 告诉设备功能协商已完成
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 驱动程序加载完成，设备可以正常工作了
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  *R(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;

  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0){
    println("virtio disk has no queue 0");
    return;
  }
  if(max < NUM){
    println("virtio disk max queue too short");
    return;
  }
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;
  memset(disk.pages, 0, sizeof(disk.pages));
  *R(VIRTIO_MMIO_QUEUE_PFN) = ((uint64)disk.pages) >> PGSHIFT;

  // desc = pages -- num * virtq_desc
  // avail = pages + 0x40 -- 2 * uint16, then num * uint16
  // used = pages + 4096 -- 2 * uint16, then num * vRingUsedElem

  disk.desc = (struct virt_desc *) disk.pages;
  disk.avail = (struct virt_avail *)(disk.pages + NUM*sizeof(struct virt_desc));
  disk.used = (struct virt_used *) (disk.pages + PGSIZE);

  // 所有 NUM 描述符开始未使用
  for(int i = 0; i < NUM; i++){
    disk.free[i] = 1;
  }
}

void virtio_disk_isr()
{
  //lock_acquire(&disk.vdisk_lock);

  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // the device increments disk.used->idx when it
  // adds an entry to the used ring.

  while (disk.used_idx != disk.used->idx) {
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;
    if (disk.info[id].status != 0){
      println("virtio_disk_intr status");
    }
    struct blk *b = disk.info[id].b;
    b->disk = 0;
    disk.used_idx += 1;
  }

  //lock_free(&disk.vdisk_lock);
}