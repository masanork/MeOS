#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t next(uint32_t *s){*s=*s*1664525u+1013904223u;return *s;}
int main(void){
 uint32_t seed=1;
 for(unsigned i=0;i<1000000;i++){
  int32_t n=(int32_t)next(&seed);int32_t d=(int32_t)(next(&seed)&0x7fffffff);if(!d)d=1;
  int32_t q=n/d,r=n%d;if(r<0){q--;r+=d;}
  double ref=floor((double)n/d);assert(q==(int32_t)ref&&r==(int32_t)((double)n-ref*d));
  uint32_t bits=next(&seed);bits=(bits&0x807fffffu)|((next(&seed)%127u)<<23);
  float z;memcpy(&z,&bits,4);assert(z>-1&&z<1);
  int old=(int)((double)z*0x8000*0x10000),fast=(int)(z*0x8000*0x10000);
  assert(old==fast);
  bits=next(&seed)&0x7f7fffffu;float positive;memcpy(&positive,&bits,4);
  float ref_sqrt=(float)sqrt((double)positive),fp32_sqrt=__builtin_sqrtf(positive);
  assert(!memcmp(&ref_sqrt,&fp32_sqrt,4));
 }
 puts("Quake render math: 1M signed floor divisions and finite normalized depth scalings / FP32 square roots match double reference PASS");
}
