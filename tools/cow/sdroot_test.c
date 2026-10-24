#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <m5os/kmalloc.h>
#include <m5os/mutex.h>
#include <m5os/sdcard.h>
#define SECTORS 65536u
static unsigned char *card;
static blockdev_t device;
static int read_sd(blockdev_t *d,uint32_t l,uint32_t n,void *p){(void)d;assert(l<SECTORS&&n<=SECTORS-l);memcpy(p,card+(size_t)l*512,n*512);return 0;}
static int write_sd(blockdev_t *d,uint32_t l,uint32_t n,const void *p){(void)d;assert(l<SECTORS&&n<=SECTORS-l);memcpy(card+(size_t)l*512,p,n*512);return 0;}
static bool fat_busy;
bool fat_is_mounted(void){return fat_busy;}
blockdev_t *sdcard_probe(void){return &device;}
void sdcard_status(sdcard_status_t *s){*s=(sdcard_status_t){.probed=true,.ready=true,.sectors=SECTORS,.high_capacity=true};}
void *kmalloc(size_t n){return malloc(n);}
void *kmalloc_psram(size_t n){return malloc(n);}
void *kcalloc(size_t n,size_t z){return calloc(n,z);}
void kfree(void *p){free(p);}
void mutex_lock(mutex_t *m){assert(!m->owner);m->owner=(void *)1;}
void mutex_unlock(mutex_t *m){assert(m->owner);m->owner=NULL;}
void kprintf(const char *fmt,...){va_list ap;va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);}
void kputs(const char *s){fputs(s,stdout);}
/* Include adapter to model process restart by closing its static file handles. */
#include "../../fs/sdroot/sdroot.c"
static void verify(const char *text){
    int fd=fs_open("/home/sd-boot.txt",FS_O_READ);assert(fd>=0);
    char b[80]={0};int n=fs_read(fd,b,sizeof b);
    assert(n==(int)strlen(text)&&!memcmp(b,text,(size_t)n));assert(!fs_close(fd));
}
int main(void){
    card=malloc((size_t)SECTORS*512);assert(card);memset(card,0xff,(size_t)SECTORS*512);
    device=(blockdev_t){.name="test-sd",.block_size=512,.block_count=SECTORS,.read=read_sd,.write=write_sd};
    assert(sdroot_mount()<0);assert(sdroot_format(SECTORS-1)<0);
    fat_busy=true;assert(sdroot_format(SECTORS)<0);fat_busy=false;
    assert(!sdroot_format(SECTORS));assert(!sdroot_mount());
    assert(!sdroot_save_probe("first-boot"));verify("first-boot");
    assert(!fs_unmount());close_files();
    assert(!sdroot_mount());verify("first-boot");
    for(int i=0;i<30;i++)assert(!sdroot_save_probe("second-boot"));
    assert(!cow_store_compact(&store));verify("second-boot");
    assert(!fs_unmount());close_files();assert(!sdroot_mount());verify("second-boot");
    assert(!fs_unmount());close_files();free(card);
    puts("SDROOT littlefs integration PASS: explicit format, wrong-card size rejection, sparse base, indexed COW, remount persistence, continued saves, compaction");
}
