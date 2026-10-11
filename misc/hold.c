/* hold.c — freestanding static: print pid then nanosleep forever.
 * No dyld, no libc — survives a half-populated shared region. */
#include <stdint.h>
static inline long svc(int n,long a,long b,long c){
    register long x0 asm("x0")=a,x1 asm("x1")=b,x2 asm("x2")=c;
    register int x16 asm("x16")=n;
    asm volatile("svc #0x80":"+r"(x0):"r"(x1),"r"(x2),"r"(x16):"memory","cc");
    return x0;
}
static int sl(const char*s){int n=0;while(s[n])n++;return n;}
static void wr(const char*s){svc(4,1,(long)s,sl(s));}
void _start(void){
    wr("HOLD alive\n");
    for(;;){struct{long s,ns;}ts={60,0};svc(334,(long)&ts,0,0);}
}
