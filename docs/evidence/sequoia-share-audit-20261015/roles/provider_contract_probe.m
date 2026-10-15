// Local host behavior probe, not a replacement provider or device deployment.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <stdio.h>
#include <string.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include "../../../../misc/cydiasubstrate_macos.m"

__attribute__((used,noinline)) static int probe_hidden_marker(void) { return 42; }
__attribute__((used,noinline,visibility("default"))) int probe_exported_marker(void) { return 61; }
static int original_method(id self, SEL sel) { (void)self;(void)sel;return 3; }
static int replacement_method(id self, SEL sel) { (void)self;(void)sel;return 7; }
static void *reference_local(const struct mach_header_64 *mh,const char *wanted) {
 const struct symtab_command *st=0;const struct segment_command_64 *text=0,*le=0;
 const char *p=(const char *)(mh+1);
 for(uint32_t i=0;i<mh->ncmds;i++){
  const struct load_command *lc=(const void *)p;
  if(lc->cmd==LC_SEGMENT_64){const struct segment_command_64 *s=(const void *)p;
   if(!strcmp(s->segname,"__TEXT"))text=s;if(!strcmp(s->segname,"__LINKEDIT"))le=s;
  }else if(lc->cmd==LC_SYMTAB)st=(const void *)p;
  p+=lc->cmdsize;
 }
 if(!st||!text||!le||st->symoff<le->fileoff||st->stroff<le->fileoff)return 0;
 uintptr_t slide=(uintptr_t)mh-text->vmaddr;
 const struct nlist_64 *nl=(const void *)(slide+le->vmaddr+st->symoff-le->fileoff);
 const char *str=(const void *)(slide+le->vmaddr+st->stroff-le->fileoff);
 for(uint32_t i=0;i<st->nsyms;i++)if(nl[i].n_un.n_strx<st->strsize &&
  (nl[i].n_type&N_TYPE)==N_SECT && !strcmp(str+nl[i].n_un.n_strx,wanted))return (void *)(slide+nl[i].n_value);
 return 0;
}
int main(void){
 setvbuf(stdout, NULL, _IONBF, 0);
 const struct mach_header_64 *mh=(const void *)_dyld_get_image_header(0);
 void *hidden=MSFindSymbol(mh,"_probe_hidden_marker");
 void *reference=reference_local(mh,"_probe_hidden_marker");
 void *exported=MSFindSymbol(0,"_probe_exported_marker");
 printf("symbol hidden=%p reference=%p expected=%p exported=%p\n",hidden,reference,(void *)probe_hidden_marker,exported);
 Class cls=objc_allocateClassPair([NSObject class],"MacPadProviderContractProbe",0);
 SEL sel=sel_registerName("probeValue");class_addMethod(cls,sel,(IMP)original_method,"i@:");objc_registerClassPair(cls);
 id obj=class_createInstance(cls,0);int before=((int(*)(id,SEL))objc_msgSend)(obj,sel);IMP old=0;
 MSHookMessageEx(cls,sel,(IMP)replacement_method,&old);
 int after=((int(*)(id,SEL))objc_msgSend)(obj,sel);int oldResult=old?((int(*)(id,SEL))old)(obj,sel):-1;
 printf("message-hook before=%d after=%d replacement-expected=7 old-callable=%d\n",before,after,oldResult);
 object_dispose(obj);
 int reproduced=hidden==0 && reference==(void *)probe_hidden_marker && exported==(void *)probe_exported_marker && before==3 && after==3 && oldResult==3;
 printf("VERDICT=%s\n",reproduced?"scaffold-contract-gaps-reproduced":"inconclusive");
 return reproduced?0:2;
}
