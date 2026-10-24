#include <m5os/board_config.h>
#if M5OS_QUAKE
#include <m5os/vfs.h>
#include <m5os/kprintf.h>
#include <m5os/hal_time.h>
#include <m5os/task.h>
#include <m5os/crc32.h>
#include <m5os/errno.h>
#include <string.h>
static bool parse(const char *s,unsigned base,uint32_t *out) {
    uint32_t n=0;unsigned count=0;
    for(;*s;s++){unsigned d=*s>='0'&&*s<='9'?*s-'0':*s>='a'&&*s<='f'?*s-'a'+10:99;if(d>=base||n>(UINT32_MAX-d)/base)return false;n=n*base+d;count++;}
    *out=n;return count!=0;
}
/* Explicit developer-only transfer. Fixed new destination, no overwrite, CRC,
 * timeout and cleanup. The runtime adapter remains read-only. */
int quake_receive(int argc,char **argv) {
    uint32_t size,want;
    if(argc!=4||!parse(argv[2],10,&size)||!parse(argv[3],16,&want)||size<12||size>64u*1024*1024){kputs("usage: quake receive BYTES CRC32hex\n");return 1;}
    const char *path="/sd/id1/pak0.pak";vfs_info_t info;
    int stat=vfs_stat(path,&info);
    if(stat!=ERR_NOENT){kputs("QFAIL destination exists or cannot be checked\n");return 1;}
    int fd=vfs_open(path,VFS_O_WRITE|VFS_O_CREATE|VFS_O_TRUNC);
    if(fd<0){kprintf("QFAIL open %d\n",fd);return 1;}
    uint8_t data[4096];uint32_t done=0,sum=0;
    kprintf("QREADY %u 4096\n",size);
    while(done<size){
        unsigned n=size-done;if(n>sizeof data)n=sizeof data;
        unsigned got=0;uint64_t deadline=hal_time_now_us()+5000000;
        while(got<n){int c=kconsole_getc();if(c>=0){data[got++]=(uint8_t)c;deadline=hal_time_now_us()+5000000;}else{if(hal_time_now_us()>deadline)goto fail;task_yield();}}
        if(vfs_write(fd,data,n)!=(int)n)goto fail;
        sum=crc32_update(sum,data,n);done+=n;kprintf("QACK %u\n",done);
    }
    if(vfs_close(fd)<0){fd=-1;goto fail;}fd=-1;
    if(sum!=want)goto fail;
    kprintf("QDONE %u %08x\n",done,sum);return 0;
fail:
    if(fd>=0)vfs_close(fd);
    int rc=vfs_unlink(path);kprintf("QFAIL %u %08x cleanup %d\n",done,sum,rc);return 1;
}
#endif
