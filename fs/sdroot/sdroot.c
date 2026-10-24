#include <m5os/board_config.h>
#if defined(M5OS_BOARD_TAB5)
#include <m5os/sdroot.h>
#include <m5os/sdcard.h>
#include <m5os/fat.h>
#include <m5os/hal_gpio.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/errno.h>
#include <m5os/crc32.h>
#include "../../third_party/littlefs/lfs.h"
#include "../cow/store.h"
#include "../cow/blockdev.h"
#include <string.h>
#define ROOT_SECTORS 16384u /* 8 MiB logical M5FS, sparse zero-filled RO base */
#define BANK_BYTES (12u*1024u*1024u) /* full 8 MiB snapshot + transaction headroom */
#define ERASE_BYTES 4096u
static blockdev_t *sd;
static lfs_t lfs;
static struct lfs_config cfg;
static unsigned char read_cache[512],prog_cache[512],lookahead[128];
static unsigned char bounce[512];
static lfs_file_t base_file;
static struct lfs_file_config base_cfg;
static unsigned char base_cache[512];
struct bank_file { lfs_file_t file; struct lfs_file_config cfg; unsigned char cache[512]; };
static struct bank_file files[2];
static struct cow_store store;
static blockdev_t root_bd;
static uint32_t *index_map;
static uint64_t identity;
static bool mounted,active,opened;
static int disk_read(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,void *p,lfs_size_t n) {
    (void)c;return sd->read(sd,b*8+off/512,n/512,p)?LFS_ERR_IO:0;
}
static int disk_prog(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,const void *p,lfs_size_t n) {
    (void)c;return sd->write(sd,b*8+off/512,n/512,p)?LFS_ERR_IO:0;
}
static int disk_erase(const struct lfs_config *c,lfs_block_t b) {
    (void)c;memset(bounce,0xff,sizeof bounce);
    for(unsigned i=0;i<8;i++)if(sd->write(sd,b*8+i,1,bounce))return LFS_ERR_IO;
    return 0;
}
static int disk_sync(const struct lfs_config *c) {
    (void)c; /* CMD24 waits for ready and checks CMD13 after every sector. */
    sdcard_status_t st;sdcard_status(&st);return st.ready?0:LFS_ERR_IO;
}
static int prepare(void) {
    sd=NULL;
    for(unsigned attempt=0;attempt<3;attempt++) {
        sd=sdcard_probe();if(sd)break;
        sdcard_status_t st;sdcard_status(&st);
        kprintf("sdroot: probe %u failed: %s (R1=0x%02x)\n",attempt+1,st.detail,st.last_r1);
        if(attempt<2)hal_delay_us(100000);
    }
    if(!sd)return ERR_NODEV;
    cfg=(struct lfs_config){.read=disk_read,.prog=disk_prog,.erase=disk_erase,.sync=disk_sync,
        .read_size=512,.prog_size=512,.block_size=ERASE_BYTES,.block_count=sd->block_count/8,
        .block_cycles=500,.cache_size=512,.lookahead_size=sizeof lookahead,
        .read_buffer=read_cache,.prog_buffer=prog_cache,.lookahead_buffer=lookahead};
    return cfg.block_count>=128?0:ERR_INVAL;
}
/* Bytes beyond EOF represent zero sectors, not an image-sized RAM buffer.
 * Only the formatter writes base.img; runtime opens it read-only. */
static int file_read(lfs_file_t *f,uint32_t o,void *p,size_t n) {
    lfs_soff_t size=lfs_file_size(&lfs,f);if(size<0)return -1;
    memset(p,0,n);if(o>=(uint32_t)size)return 0;
    size_t take=n;if(take>(uint32_t)size-o)take=(uint32_t)size-o;
    if(lfs_file_seek(&lfs,f,o,LFS_SEEK_SET)!=(lfs_soff_t)o)return -1;
    return lfs_file_read(&lfs,f,p,take)==(lfs_ssize_t)take?0:-1;
}
static int file_write(lfs_file_t *f,uint32_t o,const void *p,size_t n) {
    if(lfs_file_seek(&lfs,f,o,LFS_SEEK_SET)!=(lfs_soff_t)o)return -1;
    return lfs_file_write(&lfs,f,p,n)==(lfs_ssize_t)n?0:-1;
}
static int base_read(void *ctx,uint32_t lba,void *p){(void)ctx;return file_read(&base_file,lba*512,p,512);}
static int base_rd(blockdev_t *bd,uint32_t l,uint32_t n,void *p){(void)bd;return file_read(&base_file,l*512,p,n*512)?ERR_IO:0;}
static int base_wr(blockdev_t *bd,uint32_t l,uint32_t n,const void *p){(void)bd;return file_write(&base_file,l*512,p,n*512)?ERR_IO:0;}
static int bank_read(void *ctx,uint32_t o,void *p,size_t n){return file_read(&((struct bank_file *)ctx)->file,o,p,n);}
static int bank_write(void *ctx,uint32_t o,const void *p,size_t n){return file_write(&((struct bank_file *)ctx)->file,o,p,n);}
static int bank_sync(void *ctx){return lfs_file_sync(&lfs,&((struct bank_file *)ctx)->file);}
static int bank_reset(void *ctx){return lfs_file_truncate(&lfs,&((struct bank_file *)ctx)->file,0);}
static struct cow_bank banks[2];
static void close_files(void) {
    if(opened){for(int i=0;i<2;i++)lfs_file_close(&lfs,&files[i].file);lfs_file_close(&lfs,&base_file);opened=false;}
    if(mounted){lfs_unmount(&lfs);mounted=false;}
    kfree(index_map);index_map=NULL;active=false;
}
static int open_base(int flags) {
    memset(&base_cfg,0,sizeof base_cfg);base_cfg.buffer=base_cache;
    return lfs_file_opencfg(&lfs,&base_file,"base.img",flags,&base_cfg);
}
static int base_identity(void) {
    lfs_soff_t size=lfs_file_size(&lfs,&base_file);if(size<=0||size>64*1024)return -1;
    uint32_t sum=0;
    for(uint32_t off=0;off<(uint32_t)size;off+=512){
        if(file_read(&base_file,off,bounce,sizeof bounce))return -1;
        sum=crc32_update(sum,bounce,sizeof bounce);
    }
    identity=((uint64_t)sum<<32)|ROOT_SECTORS;return 0;
}
static int open_banks(bool create) {
    for(int i=0;i<2;i++){
        memset(&files[i],0,sizeof files[i]);files[i].cfg.buffer=files[i].cache;
        if(lfs_file_opencfg(&lfs,&files[i].file,i?"delta-b.log":"delta-a.log",LFS_O_RDWR|(create?LFS_O_CREAT:0),&files[i].cfg)){
            while(i-->0)lfs_file_close(&lfs,&files[i].file);
            lfs_file_close(&lfs,&base_file);return -1;
        }
        banks[i]=(struct cow_bank){{&files[i],BANK_BYTES,bank_read,bank_write,bank_sync},bank_reset};
    }
    opened=true;return 0;
}
static int attach_index(void){return cow_index(&store.view,index_map,ROOT_SECTORS);}
static int begin(void *ctx){(void)ctx;if(!store.view.index&&attach_index())return -1;return cow_store_begin(&store);}
static int commit(void *ctx){(void)ctx;return cow_store_commit(&store);}
static int recover(void *ctx){
    (void)ctx;
    if(cow_store_recover(&store)||attach_index())return -1;
    return store.view.sealed?cow_store_compact(&store):0;
}
int sdroot_mount(void) {
    /* Refuse competing owners before prepare can reset/probe the SD bus. */
    if(active||mounted||fat_is_mounted())return ERR_BUSY;
    int rc=prepare();if(rc)return rc;
    if(lfs_mount(&lfs,&cfg))return ERR_CORRUPT;
    mounted=true;
    if(open_base(LFS_O_RDONLY))goto fail;
    if(base_identity()){lfs_file_close(&lfs,&base_file);goto fail;}
    if(open_banks(false))goto fail;
    if(cow_store_open(&store,banks,identity,ROOT_SECTORS,base_read,NULL))goto fail;
    index_map=kmalloc_psram(ROOT_SECTORS*sizeof *index_map);if(!index_map)goto fail;
    if(attach_index()||cow_store_reserve(&store,32))goto fail;
    cow_blockdev_init(&root_bd,&store.view);
    fs_tx_backend_t backend={NULL,begin,commit,recover};
    if(fs_mount_atomic(&root_bd,0,&backend))goto fail;
    active=true;return 0;
fail:close_files();return ERR_IO;
}
int sdroot_format(uint32_t expected_sectors) {
    if(active||mounted||fat_is_mounted())return ERR_BUSY;
    int rc=prepare();if(rc)return rc;
    if(expected_sectors!=sd->block_count)return ERR_INVAL;
    kprintf("SDROOT FORMAT identified sectors=%u; replacing filesystem with littlefs\n",sd->block_count);
    if(lfs_format(&lfs,&cfg)||lfs_mount(&lfs,&cfg))return ERR_IO;
    mounted=true;
    if(open_base(LFS_O_RDWR|LFS_O_CREAT|LFS_O_TRUNC))goto fail;
    blockdev_t base={.name="base",.block_size=512,.block_count=ROOT_SECTORS,.read=base_rd,.write=base_wr};
    rc=fs_format(&base,0,ROOT_SECTORS);if(rc||lfs_file_sync(&lfs,&base_file)){
        lfs_file_close(&lfs,&base_file);goto fail;
    }
    if(base_identity()){lfs_file_close(&lfs,&base_file);goto fail;}
    if(open_banks(true))goto fail;
    if(cow_store_format(&store,banks,identity,ROOT_SECTORS,base_read,NULL))goto fail;
    close_files();kputs("SDROOT FORMAT PASS: littlefs + immutable sparse base + two delta banks; reboot to use\n");return 0;
fail:close_files();return ERR_IO;
}
bool sdroot_active(void){return active;}
void sdroot_status(void){
    sdcard_status_t st;sdcard_status(&st);
    kprintf("SDROOT active=%u card_sectors=%u high_capacity=%u littlefs=%u\n",active,st.sectors,st.high_capacity,mounted);
    if(active)kprintf("SDROOT epoch=%u bank=%u committed=%u logical_KiB=%u index_KiB=64\n",(uint32_t)store.epoch,store.selected,store.view.committed,ROOT_SECTORS/2);
}
static int save(const fs_tx_ops_t *ops,void *ctx){
    const char *text=ctx;int fd=ops->open("/home/sd-boot.txt",FS_O_WRITE|FS_O_CREATE|FS_O_TRUNC);
    if(fd<0)return fd;
    size_t n=strlen(text);int rc=ops->write(fd,text,n);if(rc!=(int)n)return rc<0?rc:ERR_IO;
    return ops->close(fd);
}
int sdroot_save_probe(const char *text){return active?fs_transaction(save,(void *)text):ERR_NODEV;}
#endif
