#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat
#include "kernel/fs.h"
#include "kernel/file.h"
#include "kernel/stat.h"

#define NINODES 200

int nlog = LOGSIZE;
int nInode = NINODES / IPB + 1;
int bitmapn = FSSIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1;
int nmeta;
int nblocks;
int freeBlock;
int fsfd;

int freeinode = 1;
char block[BSIZE];
struct superblock sb;

#define min(a,b) ((a) < (b) ? (a) : (b))

uint xint(uint x){
    uint y;
    uchar *a = (uchar*)&y;
    a[0] = x;
    a[1] = x >> 8;
    a[2] = x >> 16;
    a[3] = x >> 24;
    return y;
}

ushort xshort(ushort x){
    ushort y;
    uchar *a = (uchar*)&y;
    a[0] = x;
    a[1] = x >> 8;
    return y;
}


void wsect(int sect,void *buf){
    if(lseek(fsfd,sect * BSIZE,SEEK_SET) != sect * BSIZE){
        printf("wsect lseek die \n");
        exit(1);
    }
    if(write(fsfd,buf,BSIZE) != BSIZE){
        printf("lseek write die \n");
        exit(1);
    }
}

void rsect(uint sect,void *buf){
    if(lseek(fsfd,sect * BSIZE,SEEK_SET) != sect * BSIZE){
        printf("rsect lseek die \n");
        exit(1);
    }
    if(read(fsfd,buf,BSIZE) != BSIZE){
        printf("rsect read die \n");
        exit(1);
    }
}

void winode(uint inum,struct dinode *din){
    char buf[BSIZE];
    struct dinode *dip;
    uint bn = IBLOCK(inum,sb);
    rsect(bn,buf);
    dip = ((struct dinode*)buf) + (inum % IPB);
    *dip = *din;
    wsect(bn,buf);
}


void rinode(uint inum,struct dinode *ip){
    char buf[BSIZE];
    struct dinode *dip;
    uint bn = IBLOCK(inum,sb);
    rsect(bn,buf);
    dip = ((struct dinode*)buf) + (inum % IPB);
    printf("rinode inum:%d, size : %d\n",inum,dip->size);
    *ip = *dip;
}


uint ialloc(ushort type){
    uint inum = freeinode++;
    struct dinode din;
    bzero(&din,sizeof(din));
    din.nlink = xshort(1);
    din.type = xshort(type);
    din.size = xint(0);

    winode(inum,&din);
    return inum;
}

void iappend(uint inum,void *xp,int n){
    char *c = (char*) xp;
    struct dinode d;
    rinode(inum,&d);
    uint indirect[NINDIRECT];
    char buf[BSIZE];
    uint fbn,off,x,n1;
    off = xint(d.size);
    printf("append inum %d at off %d sz %d oldsize:%d\n", inum, off, n,d.size);
    while (n > 0) {
        fbn = off / BSIZE;
        assert(fbn < MAXFILE);
        if(fbn < NDIRECT){
            if(xint(d.addrs[fbn]) == 0){
                d.addrs[fbn] = xint(freeBlock++);
            }
            x = xint(d.addrs[fbn]);
        }else{
            if(xint(d.addrs[NDIRECT] == 0)){
                d.addrs[NDIRECT] = xint(freeBlock++);
            }
            rsect(xint(d.addrs[NDIRECT]),(char*)indirect);
            if(indirect[fbn - NDIRECT] == 0){
                indirect[fbn - NDIRECT] = xint(freeBlock++);
                wsect(xint(d.addrs[NDIRECT]), (char*)indirect);
            }
            x = xint(indirect[fbn-NDIRECT]);
        }
        n1 = min(n,(fbn+1) * BSIZE - off);
        rsect(x,buf);
        bcopy(c,buf+off - (fbn * BSIZE),n1);
        wsect(x,buf);
        n -= n1;
        off += n1;
        c += n1;
    }
    printf("d->size :%d\n",off);
    d.size = xint(off);
    winode(inum,&d);
}


void balloc(int used){
    char buf[BSIZE];
    if(used > BSIZE * 8){
        printf("used overflow...\n");
        exit(1);
    }
    bzero(buf,BSIZE);
    for(int i = 0;i < used;i++){
        buf[i / 8] = buf[i / 8] | (0x1 << (i%8));
    }
    wsect(sb.bmapstart,buf);
}

int main(int argc,char *argv[]){
    printf("---------- mkfs start -------------\n");
    for(int i =0;i< argc;i++){
        printf(argv[i]);
        printf("\n");
    }

    if(argc < 2){
        printf("argc params panic...\n");
        exit(1);
    }

    fsfd = open(argv[1],O_RDWR | O_CREAT | O_TRUNC,0666);
    if(fsfd < 0 ){
        printf("open img panic...\n");
        exit(1);
    }    

    printf(argv[1]);
    printf("\n");

    nmeta = 2 + nlog + nInode + bitmapn;
    nblocks = FSSIZE - nmeta;
   
    sb.magic = FSMAGIC;
    sb.size = xint(FSSIZE);
    sb.nblocks = xint(nblocks);
    sb.ninodes = xint(NINODES);
    sb.nlog = xint(nlog);
    sb.logstart = xint(2);
    sb.inodestart = xint(2+nlog);
    sb.bmapstart = xint(2+nlog+ninodeblocks);
    printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, bitmapn, nblocks, FSSIZE);
    freeBlock = nmeta;

    for(int i = 0; i< FSSIZE; i++){
        wsect(i,block);
    }

    char buf[BSIZE];

    memset(buf,0,sizeof(buf));
    memmove(buf,&sb,sizeof(sb));
    wsect(1,buf);

    uint rootinode = ialloc(T_DIR);
    assert(rootinode == ROOTINO);

    struct dirent de;
    bzero(&de,sizeof(de));    
    de.inum = xshort(rootinode);
    strcpy(de.name,".");
    iappend(rootinode,&de,sizeof(de));

    bzero(&de,sizeof(de));
    de.inum = xshort(rootinode);
    strcpy(de.name,"..");   
    iappend(rootinode,&de,sizeof(de));

    int fd,inum,cc;
    for(int i = 2;i < argc;i++){
        printf("<><><><><<>><><><>\n");
        char *shortname;
        if(strncmp(argv[i],"src/user/",9) == 0){
            shortname = argv[i] + 9;
        }else{
            shortname = argv[i];
        }
        // if(index(shortname,'/') == 0){
        //     printf("shortname start / \n");
        //     exit(1);
        // }

        if((fd = open(argv[i],0)) < 0){
            printf("%s not exist \n",argv[i]);
            exit(1);
        }

        if(shortname[0] == '_'){
            shortname += 1;
        }

        inum = ialloc(T_FILE);

        bzero(&de, sizeof(de));
        de.inum = xshort(inum);
        strncpy(de.name,shortname,DIRSIZ);
        printf("fullname:%s,name: %s,size: %ld\n",argv[i],shortname,sizeof(de));
        iappend(rootinode,&de,sizeof(de));

        while((cc = read(fd,buf,sizeof(buf)))  > 0){
            iappend(inum,buf,cc);
        }
        close(fd);
    }   

    struct dinode din;
    rinode(rootinode,&din);
    int off = xint(din.size);
    off = ((off / BSIZE) + 1) * BSIZE;
    din.size = off;
    winode(rootinode,&din);


    balloc(freeBlock);
    printf("mkfs success!\n");
    printf("==========\n");
    printf("sb.nblocks:%d\n",sb.nblocks);
    exit(0);
}
