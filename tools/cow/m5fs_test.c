#define _DEFAULT_SOURCE
#include "../../fs/cow/blockdev.h"
#include <m5os/fs.h>
#include <m5os/kmalloc.h>
#include <m5os/mutex.h>
#include <m5os/errno.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#define SECTORS 128u
#define CAP (64u*COW_RECORD)
static unsigned char base_image[SECTORS*512];
static unsigned char old_image[SECTORS*512],new_image[SECTORS*512];
static unsigned char *disk;
static int budget;
void *kmalloc(size_t n){return malloc(n);}
void *kmalloc_psram(size_t n){return malloc(n);}
void *kcalloc(size_t n,size_t s){return calloc(n,s);}
void kfree(void *p){free(p);}
void mutex_lock(mutex_t *m){assert(!m->owner);m->owner=(void *)1;}
void mutex_unlock(mutex_t *m){assert(m->owner);m->owner=0;}
static int base_read(void *ctx,uint32_t lba,void *p){(void)ctx;assert(lba<SECTORS);memcpy(p,base_image+lba*512,512);return 0;}
static int raw_read(blockdev_t *b,uint32_t l,uint32_t n,void *p){(void)b;assert(l+n<=SECTORS);memcpy(p,base_image+l*512,n*512);return 0;}
static int raw_write(blockdev_t *b,uint32_t l,uint32_t n,const void *p){(void)b;assert(l+n<=SECTORS);memcpy(base_image+l*512,p,n*512);return 0;}
static int rd(void *ctx,uint32_t o,void *p,size_t n){(void)ctx;assert(o+n<=CAP);memcpy(p,disk+o,n);return 0;}
static int wr(void *ctx,uint32_t o,const void *p,size_t n){
    (void)ctx;assert(o+n<=CAP);size_t take=n;
    if(budget>=0&&(size_t)budget<take)take=(size_t)budget;
    memcpy(disk+o,p,take);if(budget>=0)budget-=(int)take;
    if(take!=n)_exit(90);return 0;
}
static int sync_io(void *ctx){(void)ctx;return 0;}
static struct cow_io io={0,CAP,rd,wr,sync_io};
static void mount_overlay(struct cow *c,blockdev_t *bd){assert(!cow_mount(c,io,42,SECTORS,base_read,0));cow_blockdev_init(bd,c);}
static void write_file(const char *path,int value,size_t n,unsigned flags){
    unsigned char b[1200];memset(b,value,sizeof b);assert(n<=sizeof b);
    int fd=fs_open(path,FS_O_WRITE|flags);assert(fd>=0);
    assert(fs_write(fd,b,n)==(int)n);assert(!fs_close(fd));
}
static void mutation(int op){
    switch(op){
    case 0:write_file("/home/new",22,900,FS_O_CREATE);break;
    case 1:write_file("/home/seed",22,900,FS_O_APPEND);break;
    case 2:write_file("/home/seed",22,111,FS_O_TRUNC);break;
    case 3:assert(!fs_delete("/home/seed"));break;
    case 4:assert(!fs_mkdir("/home/sub"));break;
    case 5:{
        int a=fs_open("/home/a",FS_O_WRITE|FS_O_CREATE),b=fs_open("/home/b",FS_O_WRITE|FS_O_CREATE);
        assert(a>=0&&b>=0);assert(fs_write(a,"aaa",3)==3);assert(fs_write(b,"bbb",3)==3);
        assert(!fs_close(a));assert(!fs_close(b));break;
    }
    default:abort();
    }
}
static void run_child(int op,int cut){
    pid_t pid=fork();assert(pid>=0);
    if(!pid){
        struct cow c;blockdev_t bd;budget=cut;mount_overlay(&c,&bd);
        assert(!fs_mount(&bd,0));assert(!cow_begin(&c));mutation(op);
        assert(!fs_sync());assert(!cow_commit(&c));_exit(0);
    }
    int status;assert(waitpid(pid,&status,0)==pid);assert(WIFEXITED(status));
    assert(WEXITSTATUS(status)==0||WEXITSTATUS(status)==90);
}
static void verify(const unsigned char *expected){
    /* New process models power-on: no dirty M5FS globals survive. */
    pid_t pid=fork();assert(pid>=0);
    if(!pid){
        struct cow c;blockdev_t bd;unsigned char b[512];mount_overlay(&c,&bd);
        for(unsigned i=0;i<SECTORS;i++){assert(!cow_read(&c,i,b));assert(!memcmp(b,expected+i*512,512));}
        assert(!fs_mount(&bd,0));fs_info_t info;assert(!fs_stat("/home",&info));
        fs_stat_t st;assert(!fs_statfs(&st));assert(st.total_blocks==SECTORS);
        /* Teardown intentionally fails write_super outside a transaction, but
         * fs_unmount still frees buffers. A production RO path needs its own API. */
        assert(fs_unmount()==ERR_IO);_exit(0);
    }
    int status;assert(waitpid(pid,&status,0)==pid);assert(WIFEXITED(status)&&!WEXITSTATUS(status));
}
int main(void){
    disk=mmap(0,CAP,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(disk!=MAP_FAILED);
    blockdev_t raw={.block_size=512,.block_count=SECTORS,.read=raw_read,.write=raw_write};
    assert(!fs_format(&raw,0,SECTORS));assert(!fs_mount(&raw,0));
    write_file("/home/seed",11,600,FS_O_CREATE);assert(!fs_unmount());
    memcpy(old_image,base_image,sizeof old_image);unsigned cases=0;
    for(int op=0;op<6;op++){
        memset(disk,0,CAP);run_child(op,-1);struct cow c;blockdev_t bd;mount_overlay(&c,&bd);
        uint32_t bytes=c.committed;assert(bytes>0&&c.generation==1);
        for(unsigned i=0;i<SECTORS;i++)assert(!cow_read(&c,i,new_image+i*512));
        verify(new_image);
        const unsigned offsets[]={0,1,31,32,256,548,551};
        for(uint32_t r=0;r<bytes/COW_RECORD;r++)for(unsigned j=0;j<sizeof offsets/sizeof offsets[0];j++){
            memset(disk,0,CAP);run_child(op,(int)(r*COW_RECORD+offsets[j]));
            mount_overlay(&c,&bd);verify(c.generation?new_image:old_image);
            assert(!memcmp(base_image,old_image,sizeof old_image));cases++;
        }
    }
    assert(!munmap(disk,CAP));printf("M5FS COW PASS: %u crash cases; create/grow/truncate/delete/mkdir/multiple-open; full recovered block image old-or-new\n",cases);
}
