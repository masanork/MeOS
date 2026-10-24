#include <m5os/board_config.h>
#if M5OS_QUAKE
#include <m5os/quake.h>
#include <m5os/app.h>
#include <m5os/task.h>
#include <m5os/arch.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/display.h>
#include <m5os/hal_time.h>
#include <m5os/ime.h>
#include <m5os/vfs.h>
#include "../../drivers/lcd/fbcon.h"
#include "../../drivers/tab5kbd/tab5touch.h"
#include "runtime.h"
#define QUAKE_COMPAT_DECLARATIONS_ONLY
#include "../../components/quakeengine/compat.h"
#include "../../third_party/quakegeneric/source/quakedef.h"
#include "../../third_party/quakegeneric/source/quakegeneric.h"
#include <string.h>
uint32_t quake_owner;
char quake_basedir[96]="/sd";
extern char _quake_data_start[],_quake_data_end[],_quake_bss_start[],_quake_bss_end[],_quake_common_start[],_quake_common_end[];
static void *initial;
static jmp_buf escape;
static bool pending,screen;
static uint16_t palette[256];
static unsigned frames;
static uint64_t began;
static bool held[256],wanted[256],sampled;
static int keyboard;
static uint64_t key_until;
static void reset_engine(void) {
    memset(_quake_bss_start,0,(size_t)(_quake_bss_end-_quake_bss_start));
    memset(_quake_common_start,0,(size_t)(_quake_common_end-_quake_common_start));
    memcpy(_quake_data_start,initial,(size_t)(_quake_data_end-_quake_data_start));
}
void quake_exit(int code) { longjmp(escape,code?2:1); }
int quake_app_cleanup(uint32_t id) {
    if(id!=quake_owner||!id)return 0;
    quake_io_close_all();
    if(screen) { display_set_target(NULL);ime_bypass(false);kconsole_set_focus(0);fbcon_pause(false); }
    display_release_claim(&quake_owner);screen=false;
    if(initial)reset_engine();
    quake_owner=0;return 0;
}
void QG_Init(void) {}
void QG_Quit(void) { quake_exit(0); }
void QG_SetPalette(unsigned char p[768]) { for(int i=0;i<256;i++)palette[i]=RGB565(p[i*3],p[i*3+1],p[i*3+2]); }
void QG_DrawFrame(void *pixels) {
    display_draw_indexed_bitmap_resized(160,0,320,200,pixels,palette,960,720);
    frames++;sampled=false;
    if(!(frames%100))kprintf("quake: %u frames in %u ms\n",frames,(unsigned)((hal_time_now_us()-began)/1000));
    task_yield();
}
static void sample_inputs(void) {
    memset(wanted,0,sizeof wanted);
    int ch;while((ch=kconsole_getc())>=0) {
        if(ch==3||ch=='q')quake_exit(0);
        keyboard=ch=='w'?K_UPARROW:ch=='s'?K_DOWNARROW:ch=='a'?K_LEFTARROW:ch=='d'?K_RIGHTARROW:ch=='f'?K_CTRL:ch;
        key_until=hal_time_now_us()+180000;
    }
    if(hal_time_now_us()<key_until && keyboard>0&&keyboard<256)wanted[keyboard]=true;
    tab5_touch_point_t points[10];int count=tab5_touch_read_points(points,10);
    for(int i=0;i<count;i++) {
        int x=points[i].x,y=points[i].y,key=0;
        if(x<160)key=y<240?K_UPARROW:y<480?K_DOWNARROW:K_ESCAPE;
        else if(x>=1120)key=y<180?K_CTRL:y<360?K_SPACE:y<540?K_LEFTARROW:K_RIGHTARROW;
        if(key)wanted[key]=true;
    }
}
int QG_GetKey(int *down,int *key) {
    if(!sampled){sample_inputs();sampled=true;}
    for(int i=0;i<256;i++)if(held[i]!=wanted[i]){held[i]=wanted[i];*key=i;*down=held[i];return 1;}
    return 0;
}
void QG_GetMouseMove(int *x,int *y){*x=*y=0;}
void QG_GetJoyAxes(float *axes){for(int i=0;i<QUAKEGENERIC_JOY_MAX_AXES;i++)axes[i]=0;}
void quake_main(void *arg) {
    (void)arg;pending=false;
    if(!display_is_ready()||fbcon_is_paused()){kputs("quake: display busy/unavailable\n");return;}
    uint32_t irq=arch_irq_disable();
    if(quake_owner){arch_irq_restore(irq);return;}
    quake_owner=task_current()->app->app_id;
    arch_irq_restore(irq);
    if(!display_try_claim(&quake_owner)){quake_owner=0;return;}
    size_t n=(size_t)(_quake_data_end-_quake_data_start);
    if(!n||n>256u*1024u||(uintptr_t)_quake_bss_end<=(uintptr_t)_quake_bss_start){kputs("quake: invalid linker state\n");return;}
    if(!initial){initial=kmalloc_psram(n);if(!initial)return;memcpy(initial,_quake_data_start,n);}
    reset_engine();memset(held,0,sizeof held);memset(wanted,0,sizeof wanted);sampled=false;keyboard=0;key_until=0;
    kconsole_set_focus(task_current()->id);ime_bypass(true);fbcon_pause(true);screen=true;task_sleep(60);
    display_set_target(NULL);display_clear(COLOR_BLACK);
    display_draw_text(12,60,"FWD",COLOR_WHITE,COLOR_BLACK,2);display_draw_text(12,300,"BACK",COLOR_WHITE,COLOR_BLACK,2);display_draw_text(12,580,"MENU",COLOR_WHITE,COLOR_BLACK,2);
    display_draw_text(1140,60,"FIRE",COLOR_WHITE,COLOR_BLACK,2);display_draw_text(1140,260,"JUMP",COLOR_WHITE,COLOR_BLACK,2);display_draw_text(1140,440,"LEFT",COLOR_WHITE,COLOR_BLACK,2);display_draw_text(1140,620,"RIGHT",COLOR_WHITE,COLOR_BLACK,2);
    frames=0;began=hal_time_now_us();
    if(!setjmp(escape)) {
        quakeparms_t parms={0};parms.memsize=20*1024*1024;parms.membase=quake_malloc(parms.memsize);parms.basedir=quake_basedir;
        char *args[]={"quake","-basedir",quake_basedir,"-nosound","-nocdaudio",NULL};
        COM_InitArgv(5,args);parms.argc=com_argc;parms.argv=com_argv;
        Host_Init(&parms);
        uint64_t last=hal_time_now_us();
        for(;;){uint64_t now=hal_time_now_us();Host_Frame((now-last)/1000000.0);last=now;task_sleep(1);}
    }
    kprintf("quake: stopped after %u frames\n",frames);
}
int quake_command(int argc,char **argv) {
    if(quake_owner||pending)return 1;
    if(argc>2||(argc==2&&strcmp(argv[1],"/sd"))){kputs("usage: quake [/sd]\n");return 1;}
    vfs_info_t info;if(vfs_stat("/sd/id1/pak0.pak",&info)<0){kputs("quake: mount SD with /id1/pak0.pak first\n");return 1;}
    pending=true;uint32_t id;int rc=app_start("quake",&id);if(rc<0)pending=false;return rc<0;
}
#else
void quake_main(void *arg) {(void)arg;}
int quake_command(int argc,char **argv){(void)argc;(void)argv;return -4;}
int quake_app_cleanup(uint32_t id){(void)id;return 0;}
#endif
