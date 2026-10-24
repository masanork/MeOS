#include "../../fs/cow/store.h"
#include "../../fs/cow/blockdev.h"
#include "../../user/basic/file.h"
#include <m5os/vfs.h>
#include <m5os/app.h>
#include <m5os/mutex.h>
#include <m5os/errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SECTORS 256u
#define CAP (512u+128u*COW_RECORD)
static unsigned char base_bytes[SECTORS*512];
struct bank { unsigned char data[CAP]; };
static struct bank banks[2];
static int budget=-1,fail_sync,syncs;
static unsigned allocations;
static uint32_t permissions=APP_PERM_FS_READ|APP_PERM_FS_WRITE;
void *kmalloc(size_t n){void *p=malloc(n);if(p)allocations++;return p;}
void *kmalloc_psram(size_t n){return kmalloc(n);}
void *kcalloc(size_t n,size_t z){void *p=calloc(n,z);if(p)allocations++;return p;}
void kfree(void *p){if(p){assert(allocations);allocations--;free(p);}}
void mutex_lock(mutex_t *m){assert(!m->owner);m->owner=(void *)1;}
void mutex_unlock(mutex_t *m){assert(m->owner);m->owner=NULL;}
bool task_has_perm(uint32_t p){return (permissions&p)==p;}
task_t *task_current(void){return NULL;}
uint32_t arch_irq_disable(void){return 0;}
void arch_irq_restore(uint32_t irq){(void)irq;}
void kprintf(const char *fmt,...){(void)fmt;}
const vfs_fs_t vfs_devfs={.name="test-dev"};
static int raw_read(blockdev_t *d,uint32_t l,uint32_t n,void *p){(void)d;assert(l+n<=SECTORS);memcpy(p,base_bytes+l*512,n*512);return 0;}
static int raw_write(blockdev_t *d,uint32_t l,uint32_t n,const void *p){(void)d;assert(l+n<=SECTORS);memcpy(base_bytes+l*512,p,n*512);return 0;}
static int base_read(void *ctx,uint32_t l,void *p){(void)ctx;assert(l<SECTORS);memcpy(p,base_bytes+l*512,512);return 0;}
static int read_log(void *ctx,uint32_t o,void *p,size_t n){struct bank *b=ctx;assert(o<=CAP&&n<=CAP-o);memcpy(p,b->data+o,n);return 0;}
static int write_log(void *ctx,uint32_t o,const void *p,size_t n){
    struct bank *b=ctx;assert(o<=CAP&&n<=CAP-o);size_t take=n;
    if(budget>=0&&(size_t)budget<take)take=(size_t)budget;
    memcpy(b->data+o,p,take);if(budget>=0)budget-=(int)take;return take==n?0:-1;
}
static int reset_log(void *ctx){memset(ctx,0,sizeof(struct bank));return 0;}
static int sync_log(void *ctx){(void)ctx;return ++syncs==fail_sync?-1:0;}
static int recover(void *ctx){budget=-1;fail_sync=0;return cow_store_abort(ctx);}
static void output(const char *s,size_t n,void *ctx){(void)s;(void)n;(void)ctx;}
static void expect(const char *listing){
    char b[128]={0};int fd=vfs_open("/home/work.bas",VFS_O_READ);assert(fd>=0);
    int n=vfs_read(fd,b,sizeof b);assert(n==(int)strlen(listing)&&!memcmp(b,listing,(size_t)n));assert(!vfs_close(fd));
}
int main(void){
    blockdev_t raw={.block_size=512,.block_count=SECTORS,.read=raw_read,.write=raw_write};
    assert(!fs_format(&raw,0,SECTORS));
    struct cow_bank io[2];for(unsigned i=0;i<2;i++)io[i]=(struct cow_bank){{&banks[i],CAP,read_log,write_log,sync_log},reset_log};
    struct cow_store store;assert(!cow_store_format(&store,io,42,SECTORS,base_read,NULL));
    blockdev_t bd;cow_blockdev_init(&bd,&store.view);
    fs_tx_backend_t backend={.ctx=&store,.begin=cow_store_begin,.commit=cow_store_commit,.recover=recover};
    assert(!fs_mount_atomic(&bd,0,&backend));assert(!vfs_mount("/",&vfs_m5fs,NULL));
    basic_session *s=kmalloc(sizeof *s);assert(s);basic_session_init(s,output,NULL,NULL);
    assert(!basic_session_submit(s,"10 PRINT .125"));basic_file_result result;
    assert(!basic_file_save(s,"/home/work.bas",&result));expect("10 PRINT .125\n");
    assert(!basic_session_submit(s,"10 PRINT .375"));
    basic_program edited=s->source;
    budget=13;assert(basic_file_save(s,"/home/work.bas",&result)<0&&result.bytes==0);
    assert(!memcmp(&s->source,&edited,sizeof edited));expect("10 PRINT .125\n");
    permissions=APP_PERM_FS_READ;
    assert(basic_file_save(s,"/home/work.bas",&result)==ERR_PERM&&result.bytes==0);
    assert(!memcmp(&s->source,&edited,sizeof edited));expect("10 PRINT .125\n");permissions|=APP_PERM_FS_WRITE;
    fail_sync=syncs+2;assert(basic_file_save(s,"/home/work.bas",&result)<0&&result.bytes==0);
    assert(!memcmp(&s->source,&edited,sizeof edited));expect("10 PRINT .375\n"); /* completed but unconfirmed commit */
    assert(!basic_file_save(s,"/home/work.bas",&result)&&result.bytes==14);
    assert(!fs_unmount());assert(!vfs_umount("/"));assert(!cow_store_open(&store,io,42,SECTORS,base_read,NULL));
    assert(!fs_mount_atomic(&bd,0,&backend));assert(!vfs_mount("/",&vfs_m5fs,NULL));
    assert(!basic_session_submit(s,"NEW"));assert(!basic_file_load(s,"/home/work.bas",&result));
    assert(!basic_session_run(s));expect("10 PRINT .375\n");
    assert(!vfs_count_owned(0));assert(!vfs_umount("/"));assert(!fs_unmount());kfree(s);assert(!allocations);
    puts("BASIC SAVE through real VFS/M5FS/COW: source retained, rollback, denied save, uncertain commit, remount/load/run PASS");
}
