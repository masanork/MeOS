#pragma once
#include "bitmap_resize.h"
/* Baseline 512x300 -> 1024x600, centered. Same word damage scan and column-copy
 * scheme as the low-resolution port; no renderer optimizations are added. */
static inline bool tab5_hires_damage(const uint8_t *pixels,uint8_t *previous,bool force,
    uint16_t first[512],uint16_t last[512],unsigned *left,unsigned *right){
 for(unsigned x=0;x<512;x++){first[x]=300;last[x]=0;}
 if(force){for(unsigned x=0;x<512;x++){first[x]=0;last[x]=299;}memcpy(previous,pixels,512*300);}
 else if(!(((uintptr_t)pixels|(uintptr_t)previous)&3u)){
  typedef uint32_t word __attribute__((may_alias));const word *now=(const word *)pixels;word *old=(word *)previous;
  for(unsigned y=0;y<300;y++)for(unsigned x=0;x<512;x+=4){
   unsigned at=(y*512+x)/4;uint32_t value=now[at],diff=value^old[at];if(!diff)continue;old[at]=value;
   for(unsigned b=0;b<4;b++,diff>>=8)if(diff&255u){if(first[x+b]==300)first[x+b]=(uint16_t)y;last[x+b]=(uint16_t)y;}
  }
 } else for(unsigned y=0;y<300;y++)for(unsigned x=0;x<512;x++){
  unsigned at=y*512+x;uint8_t value=pixels[at];
  if(previous[at]!=value){if(first[x]==300)first[x]=(uint16_t)y;last[x]=(uint16_t)y;previous[at]=value;}
 }
 *left=512;*right=0;for(unsigned x=0;x<512;x++)if(first[x]<300){if(*left==512)*left=x;*right=x;}
 return *left!=512;
}
static inline bool tab5_hires_delta(uint16_t *fb,const uint8_t *pixels,uint8_t *previous,
    const uint16_t palette[256],bool force,int *first_col,int *last_col){
 if(!fb||!pixels||!previous||!palette)return false;
 uint16_t first[512],last[512];unsigned left,right;
 if(!tab5_hires_damage(pixels,previous,force,first,last,&left,&right))return false;
 uint32_t column[300] __attribute__((aligned(16)));
 for(unsigned x=left;x<=right;x++)if(first[x]<300){
  for(unsigned y=first[x];y<=last[x];y++){uint32_t color=palette[pixels[y*512+x]];column[y]=color|(color<<16);}
  for(unsigned repeat=0;repeat<2;repeat++)tab5_copy_column(fb+(1279-(128+x*2+repeat))*720+60+first[x]*2,
   (const uint16_t *)column+first[x]*2,(last[x]-first[x]+1)*2);
 }
 *first_col=128+(int)left*2;*last_col=128+(int)right*2+1;return true;
}
