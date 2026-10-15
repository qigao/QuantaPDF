/* #360 test-only decimal/span/grid contract. No Graph or SDK mutation. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

typedef struct { int64_t units; unsigned scale; } dec;
static const uint64_t pow10[] = {
  1ULL,10ULL,100ULL,1000ULL,10000ULL,100000ULL,1000000ULL,
  10000000ULL,100000000ULL,1000000000ULL
};
static int parse(const char *s, dec *out) {
  size_t n = strlen(s), i=0;
  unsigned integer_digits=0, fraction_digits=0, dot=0, digits=0;
  uint64_t v=0; int neg=0;
  *out=(dec){0,0};
  if (!n || n>32) return 0;
  if (s[i]=='+' || s[i]=='-') neg=(s[i++]=='-');
  for (;i<n;++i) {
    unsigned char c=(unsigned char)s[i];
    if (c=='.' && !dot) { dot=1; continue; }
    if (c<'0' || c>'9') return 0;
    ++digits;
    if (dot) { if (++fraction_digits>9) return 0; }
    else if (++integer_digits>9) return 0;
    if (v>(INT64_MAX-(uint64_t)(c-'0'))/10) return 0;
    v=v*10+(uint64_t)(c-'0');
  }
  if (!digits) return 0;
  while (fraction_digits && v%10==0) { v/=10; --fraction_digits; }
  out->units=neg?-(int64_t)v:(int64_t)v;
  out->scale=fraction_digits;
  return 1;
}
static int aligned(dec a, dec b, int64_t *x, int64_t *y,
                   uint64_t *denom) {
  unsigned scale=a.scale>b.scale?a.scale:b.scale;
  uint64_t ma=pow10[scale-a.scale], mb=pow10[scale-b.scale];
  if (a.units>INT64_MAX/(int64_t)ma || a.units<INT64_MIN/(int64_t)ma ||
      b.units>INT64_MAX/(int64_t)mb || b.units<INT64_MIN/(int64_t)mb)
    return 0;
  *x=a.units*(int64_t)ma; *y=b.units*(int64_t)mb;
  *denom=pow10[scale]; return 1;
}
/* Positive span only; integer strict ceil without any float or epsilon. */
static int grid(const char *lo,const char *hi,unsigned userunit,
                unsigned dpi,uint64_t *result) {
  dec a,b; int64_t x,y; uint64_t denom,span,numerator,divider;
  *result=0;
  if (!parse(lo,&a)||!parse(hi,&b)||
      !aligned(a,b,&x,&y,&denom)||x>=y||
      userunit==0 || !(dpi==72||dpi==96||dpi==144))
    return 0;
  if (x<0 && y>INT64_MAX+x) return 0;
  span=(uint64_t)(y-x);
  if (span>UINT64_MAX/userunit) return 0;
  numerator=span*userunit;
  if (numerator>UINT64_MAX/dpi) return 0;
  numerator*=dpi;
  if (denom>UINT64_MAX/72) return 0;
  divider=denom*72;
  *result=numerator/divider+(numerator%divider!=0);
  return *result>0;
}
static int eq(const char *lo,const char *hi,unsigned unit,unsigned dpi,
              uint64_t expected) {
  uint64_t got=0;
  if (!grid(lo,hi,unit,dpi,&got)||got!=expected) {
    fprintf(stderr,"%s -> %s @%u/%u: got %" PRIu64 " expected %" PRIu64 "\n",
            lo,hi,unit,dpi,got,expected);
    return 0;
  }
  return 1;
}
int main(void) {
  const char *bad[]={"","-","+","-.","1e2","NaN","Inf","1,5",
    "1.2.3","1/2","1 2","1.1234567890","1234567890.1"};
  dec d; uint64_t value=999;
  for (size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i)
    if (parse(bad[i],&d)) return 1;
  if (!eq("2.375","12.375",7,72,70) ||
      !eq("2.3750","12.37500",7,144,140) ||
      !eq("3.375","11.375",7,72,56) ||
      !eq("-2.375","+7.6250",7,72,70) ||
      !eq("2.375","12.3751",7,72,71) ||
      !eq("2.375","12.3751",7,144,141) ||
      !eq("0","10.0001",1,72,11) ||
      !eq("0","10",1,72,10) ||
      !eq("0","0.125",1,72,1) ||
      !eq("0","0.875",1,144,2)) return 1;
  if (grid("12.375","2.375",7,72,&value) || value!=0) return 1;
  if (grid("0","1",7,120,&value) || value!=0) return 1;
  if (grid("0","999999999.999999999",7,144,&value) || value!=0) return 1;
  puts("decimal-exact-contract-v0: PASS (isolated test-only arithmetic)");
  return 0;
}
