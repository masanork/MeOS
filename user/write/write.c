#include <m5os/board_config.h>
#if M5OS_BOARD_TAB5
#include "write.h"
#include "../../drivers/lcd/bizud.h"
#include "../../drivers/lcd/fbcon.h"
#include "../editor/bizime.h"
#include "../scheme/buffer.h"
#include "../tui/dqwin.h"
#include <m5os/display.h>
#include <m5os/errno.h>
#include <m5os/ime.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <m5os/vfs.h>
#include <string.h>
#define BG RGB565(248,247,242)
#define FG RGB565(36,39,42)
#define ACCENT RGB565(25,85,115)
#define LEFT 48
#define RIGHT (DISPLAY_WIDTH-48)
#define HEADER_BASELINE 30
#define HEADER_RULE 40
#define H_BASELINE 70
#define H_PITCH 36
#define H_BOTTOM (DISPLAY_HEIGHT-12)
#define H_ROWS (((H_BOTTOM-H_BASELINE)/H_PITCH)+1)
#define V_TOP 48
#define V_BOTTOM (DISPLAY_HEIGHT-36)
#define V_COLUMN_PITCH 40
#define V_TRACKING 6
#define V_COLUMNS (((RIGHT-LEFT-24)/V_COLUMN_PITCH)+1)
#define CTRL(c) ((c)-'a'+1)
typedef struct {
    scheme_buffer b;
    size_t starts[SCHEME_SOURCE_CAP+2];
    int lines,top;
    bool confirm,vertical;
    uint16_t *frame;
    char status[120];
} writer;

static void *editor_alloc(size_t bytes) {
    void *p=kmalloc_psram(bytes);
    if(!p&&bizud_trim())p=kmalloc_psram(bytes);
    return p;
}
static uint32_t at(writer *w,size_t p) {const char *s=w->b.text+p;return bizud_decode(&s);}
static int hwidth(writer *w,size_t p) {uint32_t cp=at(w,p);return cp=='\t'?56:cp=='\r'?0:bizud_width(cp);}
static int vstep_cp(uint32_t cp) {return bizud_vertical_advance(cp)+V_TRACKING;}
static int vstep(writer *w,size_t p) {return vstep_cp(at(w,p));}

static bool prohibited_at_column_start(uint32_t cp) {
    switch(cp) {
    case 0x2019:case 0x201d:case 0x3001:case 0x3002:case 0x3005:
    case 0x3009:case 0x300b:case 0x300d:case 0x300f:case 0x3011:
    case 0x3015:case 0x3017:case 0x3019:case 0x301b:case 0x309d:case 0x309e:
    case 0x30fb:case 0x30fc:case 0x30fd:case 0x30fe:
    case 0xff01:case 0xff09:case 0xff0c:case 0xff0e:case 0xff1a:case 0xff1b:
    case 0xff1f:case 0xff3d:case 0xff5d:
    case 0x3041:case 0x3043:case 0x3045:case 0x3047:case 0x3049:
    case 0x3063:case 0x3083:case 0x3085:case 0x3087:case 0x308e:
    case 0x30a1:case 0x30a3:case 0x30a5:case 0x30a7:case 0x30a9:
    case 0x30c3:case 0x30e3:case 0x30e5:case 0x30e7:case 0x30ee:
    case 0x30f5:case 0x30f6:return true;
    default:return false;
    }
}
static bool prohibited_at_column_end(uint32_t cp) {
    switch(cp) {
    case 0x2018:case 0x201c:case 0x3008:case 0x300a:case 0x300c:
    case 0x300e:case 0x3010:case 0x3014:case 0x3016:case 0x3018:
    case 0x301a:case 0xff08:case 0xff3b:case 0xff5b:return true;
    default:return false;
    }
}

static void layout_horizontal(writer *w) {
    int x=LEFT;w->lines=1;w->starts[0]=0;
    for(size_t p=0;p<w->b.length;) {
        uint32_t cp=at(w,p);int advance=hwidth(w,p);
        if(cp!='\n'&&x+advance>RIGHT){w->starts[w->lines++]=p;x=LEFT;}
        p=scheme_buffer_next(&w->b,p);
        if(cp=='\n'){w->starts[w->lines++]=p;x=LEFT;}else x+=advance;
    }
    w->starts[w->lines]=w->b.length;
}

static void layout_vertical(writer *w) {
    int y=V_TOP;w->lines=1;w->starts[0]=0;
    for(size_t p=0;p<w->b.length;) {
        uint32_t cp=at(w,p);int step=vstep_cp(cp);size_t next=scheme_buffer_next(&w->b,p);
        bool wrap=cp!='\n'&&y+step>V_BOTTOM;
        if(!wrap&&cp!='\n'&&prohibited_at_column_end(cp)&&next<w->b.length) {
            uint32_t following=at(w,next);
            if(following!='\n'&&y+step+vstep_cp(following)>V_BOTTOM)wrap=true;
        }
        /* One closing mark may hang into the reserved lower margin instead of
         * becoming the first character of the next column. */
        if(wrap&&prohibited_at_column_start(cp)&&y<=V_BOTTOM)wrap=false;
        if(wrap){w->starts[w->lines++]=p;y=V_TOP;}
        p=next;
        if(cp=='\n'){w->starts[w->lines++]=p;y=V_TOP;}else y+=step;
    }
    w->starts[w->lines]=w->b.length;
}
static void layout(writer *w) {if(w->vertical)layout_vertical(w);else layout_horizontal(w);}
static int cursor_line(writer *w) {int n=0;while(n+1<w->lines&&w->starts[n+1]<=w->b.cursor)n++;return n;}
static int cursor_hx(writer *w,int line) {int x=LEFT;for(size_t p=w->starts[line];p<w->b.cursor;p=scheme_buffer_next(&w->b,p))x+=hwidth(w,p);return x;}
static int cursor_vy(writer *w,int line) {int y=V_TOP;for(size_t p=w->starts[line];p<w->b.cursor;p=scheme_buffer_next(&w->b,p))y+=vstep(w,p);return y;}

static void move_across_lines(writer *w,int delta) {
    int line=cursor_line(w),target=line+delta;
    if(target<0)target=0;
    if(target>=w->lines)target=w->lines-1;
    size_t p=w->starts[target];
    if(w->vertical) {
        int wanted=cursor_vy(w,line),y=V_TOP;
        while(p<w->starts[target+1]&&at(w,p)!='\n') {
            int step=vstep(w,p);if(y+step/2>=wanted)break;y+=step;p=scheme_buffer_next(&w->b,p);
        }
    } else {
        int wanted=cursor_hx(w,line),x=LEFT;
        while(p<w->starts[target+1]&&at(w,p)!='\n') {
            int advance=hwidth(w,p);if(x+advance/2>=wanted)break;x+=advance;p=scheme_buffer_next(&w->b,p);
        }
    }
    if(target+1<w->lines&&p==w->starts[target+1]&&p>w->starts[target])p=scheme_buffer_prev(&w->b,p);
    w->b.cursor=p;
}

static void move_inside_column(writer *w,int delta) {
    int column=cursor_line(w);size_t begin=w->starts[column],end=w->starts[column+1];
    if(delta<0) {if(w->b.cursor>begin)w->b.cursor=scheme_buffer_prev(&w->b,w->b.cursor);}
    else if(w->b.cursor<end) {
        size_t next=scheme_buffer_next(&w->b,w->b.cursor);
        if(next<end||(column==w->lines-1&&next==end))w->b.cursor=next;
    }
}

static void draw_horizontal_tail(writer *w,bizime_point point) {
    int bottom=H_BOTTOM,x=point.x,y=point.baseline;
    for(size_t p=w->b.cursor;p<w->b.length;p=scheme_buffer_next(&w->b,p)) {
        uint32_t cp=at(w,p);if(cp=='\n'){x=LEFT;y+=H_PITCH;continue;}
        int advance=hwidth(w,p);if(x+advance>RIGHT){x=LEFT;y+=H_PITCH;}
        if(y>bottom)break;
        if(cp!='\t'&&cp!='\r')bizud_draw(x,y,cp,FG,BG);
        x+=advance;
    }
}

static void draw_vertical_tail(writer *w,bizime_vpoint point) {
    int x=point.x,y=point.y;
    for(size_t p=w->b.cursor;p<w->b.length;) {
        uint32_t cp=at(w,p);size_t next=scheme_buffer_next(&w->b,p);
        if(cp=='\n'){x-=V_COLUMN_PITCH;y=V_TOP;p=next;continue;}
        int step=vstep_cp(cp);
        bool wrap=y+step>V_BOTTOM;
        if(!wrap&&prohibited_at_column_end(cp)&&next<w->b.length) {
            uint32_t following=at(w,next);
            if(following!='\n'&&y+step+vstep_cp(following)>V_BOTTOM)wrap=true;
        }
        if(wrap&&prohibited_at_column_start(cp)&&y<=V_BOTTOM)wrap=false;
        if(wrap){x-=V_COLUMN_PITCH;y=V_TOP;}
        if(x<LEFT)break;
        if(cp!='\t'&&cp!='\r')bizud_draw_vertical(x,y,cp,FG,BG);
        y+=step;p=next;
    }
}

static void paint_horizontal(writer *w,const ime_view *view) {
    int line=cursor_line(w);
    if(line<w->top)w->top=line;
    if(line>=w->top+H_ROWS)w->top=line-H_ROWS+1;
    for(int r=w->top;r<w->lines&&r<w->top+H_ROWS;r++) {
        int x=LEFT,y=H_BASELINE+(r-w->top)*H_PITCH;
        for(size_t p=w->starts[r];p<w->starts[r+1];p=scheme_buffer_next(&w->b,p)) {
            uint32_t cp=at(w,p);if(cp=='\n')break;if(cp!='\t'&&cp!='\r')bizud_draw(x,y,cp,FG,BG);x+=hwidth(w,p);
        }
    }
    int cx=cursor_hx(w,line),cy=H_BASELINE+(line-w->top)*H_PITCH;
    if(view->count) {
        int bottom=H_BOTTOM,next=cy+H_PITCH-27;
        display_fill_rect(cx,cy-27,RIGHT-cx,32,BG);
        if(next<bottom)display_fill_rect(LEFT,next,RIGHT-LEFT,bottom-next,BG);
        bizime_point after=bizime_measure(view,cx,cy,LEFT,RIGHT,H_PITCH);
        draw_horizontal_tail(w,after);
        bizime_draw(view,cx,cy,LEFT,RIGHT,HEADER_RULE+2,H_BOTTOM,H_PITCH,FG,BG,ACCENT);
    } else display_fill_rect(cx,cy-26,2,31,ACCENT);
}

static void paint_vertical(writer *w,const ime_view *view) {
    int column=cursor_line(w);
    if(column<w->top)w->top=column;
    if(column>=w->top+V_COLUMNS)w->top=column-V_COLUMNS+1;
    for(int c=w->top;c<w->lines&&c<w->top+V_COLUMNS;c++) {
        int x=RIGHT-24-(c-w->top)*V_COLUMN_PITCH,y=V_TOP;
        for(size_t p=w->starts[c];p<w->starts[c+1];p=scheme_buffer_next(&w->b,p)) {
            uint32_t cp=at(w,p);if(cp=='\n')break;
            if(cp!='\t'&&cp!='\r')bizud_draw_vertical(x,y,cp,FG,BG);
            y+=vstep_cp(cp);
        }
    }
    int cx=RIGHT-24-(column-w->top)*V_COLUMN_PITCH,cy=cursor_vy(w,column);
    if(view->count) {
        if(cx>LEFT)display_fill_rect(LEFT,V_TOP,cx-LEFT,V_BOTTOM-V_TOP+28,BG);
        display_fill_rect(cx,cy,24,V_BOTTOM-cy+28,BG);
        bizime_vpoint after=bizime_measure_vertical(view,cx,cy,V_TOP,V_BOTTOM,V_COLUMN_PITCH,V_TRACKING);
        draw_vertical_tail(w,after);
        bizime_draw_vertical(view,cx,cy,LEFT,RIGHT,V_TOP,V_BOTTOM,
                             V_COLUMN_PITCH,V_TRACKING,FG,BG,ACCENT);
    } else display_fill_rect(cx,cy,24,2,ACCENT);
}

static void paint(writer *w,const char *path,const ime_view *view) {
    display_set_target(w->frame);display_fill_rect(0,0,DISPLAY_WIDTH,DISPLAY_HEIGHT,BG);
    char header[256];
    ksnprintf(header,sizeof header,"WRITE %s%s  ^S保存 ^Q終了 ^Z戻す ^T%s  IME:%s(C-SPC)%s%s  %s",
              w->vertical?"縦":"横",w->b.modified?"*":"",w->vertical?"横":"縦",
              ime_status(),w->status[0]?"  ":"",w->status,path);
    bizud_text(LEFT,HEADER_BASELINE,header,RIGHT,view->active?ACCENT:FG,BG);
    display_fill_rect(LEFT,HEADER_RULE,RIGHT-LEFT,1,ACCENT);
    if(w->vertical)paint_vertical(w,view);else paint_horizontal(w,view);
    display_present(0,0,DISPLAY_WIDTH,DISPLAY_HEIGHT);display_set_target(NULL);
}

static bool save(writer *w,const char *path) {
    int rc=vfs_replace_file(path,w->b.text,w->b.length,NULL);
    if(rc<0)ksnprintf(w->status,sizeof w->status,"保存に失敗しました (%d)",rc);
    else {w->b.modified=false;ksnprintf(w->status,sizeof w->status,"保存しました");}
    return rc>=0;
}

bool write_file(const char *path) {
    writer *w=editor_alloc(sizeof *w);if(!w)return false;
    memset(w,0,sizeof *w);w->vertical=true;vfs_info_t st;
    int stat=vfs_stat(path,&st);
    if(stat==ERR_OK) {
        if(st.type!=FS_TYPE_FILE||st.size>SCHEME_SOURCE_CAP){kfree(w);return false;}
        int fd=vfs_open(path,VFS_O_READ);if(fd<0){kfree(w);return false;}
        size_t n=0;int got=0;while(n<st.size&&(got=vfs_read(fd,w->b.text+n,st.size-n))>0)n+=(size_t)got;
        vfs_close(fd);if(got<0||n!=st.size||!scheme_utf8_text(w->b.text,n)){kfree(w);return false;}w->b.length=n;
    } else if(stat!=ERR_NOENT){kfree(w);return false;}
    if(!display_try_claim(w)){kfree(w);return false;}w->frame=editor_alloc(DISPLAY_BUFFER_BYTES);
    if(!w->frame||!bizud_begin()){kfree(w->frame);display_release_claim(w);kfree(w);return false;}
    fbcon_pause(true);ime_set_app_view(true);bool run=true,ok=true,redraw=true;uint32_t revision=~0u;
    ime_view *view=editor_alloc(sizeof *view);
    if(!view){ime_set_app_view(false);fbcon_pause(false);display_release_claim(w);bizud_end();kfree(w->frame);kfree(w);return false;}
    while(run) {
        ime_read_view(view);if(redraw||revision!=view->revision){layout(w);paint(w,path,view);revision=view->revision;redraw=false;}
        int k=dq_pollkey();if(k==DQK_NONE){task_sleep(10);continue;}redraw=true;
        if(k==CTRL('q')){if(!w->b.modified||w->confirm)run=false;else{w->confirm=true;ksnprintf(w->status,sizeof w->status,"未保存です。もう一度 Ctrl-Q で終了 / Ctrl-S で保存");}continue;}
        w->confirm=false;w->status[0]=0;
        switch(k) {
        case CTRL('s'):save(w,path);break;
        case CTRL('z'):scheme_buffer_undo(&w->b);break;
        case CTRL('t'):w->vertical=!w->vertical;w->top=0;ksnprintf(w->status,sizeof w->status,w->vertical?"縦書きに切り替えました":"横書きに切り替えました");break;
        case DQK_LEFT:if(w->vertical)move_across_lines(w,1);else w->b.cursor=scheme_buffer_prev(&w->b,w->b.cursor);break;
        case DQK_RIGHT:if(w->vertical)move_across_lines(w,-1);else w->b.cursor=scheme_buffer_next(&w->b,w->b.cursor);break;
        case DQK_UP:if(w->vertical)move_inside_column(w,-1);else move_across_lines(w,-1);break;
        case DQK_DOWN:if(w->vertical)move_inside_column(w,1);else move_across_lines(w,1);break;
        case DQK_PGUP:move_across_lines(w,w->vertical?-V_COLUMNS:-H_ROWS);break;
        case DQK_PGDN:move_across_lines(w,w->vertical?V_COLUMNS:H_ROWS);break;
        case DQK_HOME:
            if(w->vertical)w->b.cursor=w->starts[cursor_line(w)];
            else while(w->b.cursor&&w->b.text[w->b.cursor-1]!='\n')w->b.cursor=scheme_buffer_prev(&w->b,w->b.cursor);
            break;
        case DQK_END:
            if(w->vertical){int c=cursor_line(w);size_t end=w->starts[c+1];if(c+1<w->lines&&end>w->starts[c])end=scheme_buffer_prev(&w->b,end);w->b.cursor=end;}
            else while(w->b.cursor<w->b.length&&w->b.text[w->b.cursor]!='\n')w->b.cursor=scheme_buffer_next(&w->b,w->b.cursor);
            break;
        case DQK_BACKSPACE:scheme_buffer_delete(&w->b,true);break;
        case DQK_DEL:scheme_buffer_delete(&w->b,false);break;
        default:{char bytes[4];size_t n=1;bytes[0]=(char)k;
            if(k==DQK_ENTER)bytes[0]='\n';
            else if(k>=0xc2&&k<=0xf4){n=k>=0xf0?4:k>=0xe0?3:2;for(size_t i=1;i<n;i++)bytes[i]=(char)dq_getkey();}
            else if(k!='\t'&&(k<32||k>=127))break;
            if(!scheme_buffer_insert(&w->b,bytes,n))
                ksnprintf(w->status,sizeof w->status,"入力できません（最大16 KB）");
            break;
        }}
    }
    ime_set_app_view(false);ime_clear_anchor();fbcon_pause(false);display_release_claim(w);kfree(view);bizud_end();kfree(w->frame);kfree(w);return ok;
}
#endif
