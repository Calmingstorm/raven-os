/* Disposable integrated guest: production memory/exception/timeline code, no disks.
 * MMIO mappings are inspected, never read or written as device memory. */
#include "../kernel/kernel.h"
#ifndef ROUND2_MODE
#define ROUND2_MODE 0
#endif
#define MIB (1024u*1024u)
static MultibootInfo info;
static MultibootMmapEntry map[4];
static uint32_t assertions;
static char capture[4096];
static size_t captured;
static int capturing;
void vga_set_color(uint8_t a,uint8_t b) {(void)a;(void)b;}
void vga_putc(char c) {
    if(capturing && captured+1<sizeof(capture)){capture[captured++]=c;capture[captured]=0;}
    serial_putc(c);
}
void vga_write(const char *s){while(s && *s)vga_putc(*s++);}
void vga_write_hex(uint32_t n){vga_write("0x");for(int s=28;s>=0;s-=4)vga_putc("0123456789ABCDEF"[(n>>s)&15]);}
void vga_write_dec(uint32_t n){char b[11];unsigned i=0;do{b[i++]=(char)('0'+n%10);n/=10;}while(n);while(i)vga_putc(b[--i]);}
void timer_on_irq(void){}
void task_tick(void){}
void process_tick(void){}
void keyboard_on_irq(void){}
uint32_t syscall_from_registers(Registers *r){(void)r;return 0;}
static void check(int ok,const char *name){
    assertions++;if(ok)return;
    vga_write("ROUND2 FAIL ");vga_write(name);vga_write(" assertion=");vga_write_dec(assertions);vga_putc('\n');
    outb(0xf4,0x11);for(;;)__asm__ volatile("cli; hlt");
}
static int contains(const char *s,const char *n){for(;*s;s++)if(!strncmp(s,n,strlen(n)))return 1;return 0;}
static uint32_t *directory(void){uint32_t n;__asm__ volatile("mov %%cr3,%0":"=r"(n));return(void *)(n&~4095u);}
static uint32_t pte(uint32_t va){
    uint32_t p=directory()[va>>22];if(!(p&1))return 0;
    check(!(p&0x80),"4KiB page tables");return((uint32_t *)(p&~4095u))[(va>>12)&1023];
}
/* Ignore hardware accessed/dirty bits when checking transactional failure. */
static uint32_t page_hash(void){
    uint32_t h=2166136261u,*pd=directory();
    for(unsigned i=0;i<1024;i++){
        h=(h^(pd[i]&~0x60u))*16777619u;if(!(pd[i]&1))continue;
        uint32_t *pt=(void *)(pd[i]&~4095u);
        for(unsigned j=0;j<1024;j++)h=(h^(pt[j]&~0x60u))*16777619u;
    }return h;
}
static void fixture(void){
    map[0]=(MultibootMmapEntry){20,MIB,7u*MIB,1};
    map[1]=(MultibootMmapEntry){20,9u*MIB,55u*MIB,1};
    map[2]=(MultibootMmapEntry){20,12u*MIB+123,100,2};
    map[3]=(MultibootMmapEntry){20,0x100000000ULL,MIB,1};
    info.flags=0x40;info.mmap_addr=(uintptr_t)map;info.mmap_length=sizeof(map);
    bootinfo_init(MULTIBOOT_BOOTLOADER_MAGIC,(uintptr_t)&info);pmm_init();
}
static void test_ram(void){
    check((pte(63u*MIB)&~4095u)==63u*MIB,"usable RAM above4MiB mapped");
    volatile uint32_t *high=(void *)(63u*MIB);*high=0x67452301;high[1023]=0xefcdab89;
    check(*high==0x67452301 && high[1023]==0xefcdab89,"high RAM writable");
    for(uint32_t p=8u*MIB;p<9u*MIB;p+=4096)check(!(pte(p)&1),"synthetic hole absent");
    check(!(pte(12u*MIB)&1),"overlapping reservation absent");
    check(!(pte(64u*MIB)&1),"outside available RAM absent");
    uint32_t before=pmm_free_frames();pmm_free_frame(8u*MIB);pmm_free_frame(12u*MIB);
    check(pmm_free_frames()==before,"reserved frames remain unavailable");
    check(!paging_alloc_heap(0) && !paging_alloc_heap(4097),"invalid heap map size");
    vga_write("ROUND2 PASS RAM holes reservations high-read-write\n");
}
static uint8_t pattern(uint32_t i){return(uint8_t)((i*37u)^(i>>8)^(i>>16));}
static void test_heap(void){
    heap_init();check(!kmalloc(0),"zero allocation");
    check(!kmalloc(SIZE_MAX) && !kmalloc(16u*MIB),"oversize rounding overflow");
    uint8_t *big=kmalloc(6u*MIB),*room=kmalloc(2u*MIB);
    check(big && room,"6MiB plus2MiB headroom");
    check(!((uintptr_t)big&15u) && !((uintptr_t)room&15u),"16-byte alignment");
    for(uint32_t i=0;i<16u*MIB;i+=4096)check((pte(0xc0000000u+i)&7)==3,"full16MiB supervisor RW arena");
    for(uint32_t i=0;i<6u*MIB;i++)big[i]=pattern(i);
    for(uint32_t i=0;i<2u*MIB;i++)room[i]=(uint8_t)(pattern(i)^0xffu);
    int good=1;
    for(uint32_t i=0;i<6u*MIB;i++)if(big[i]!=pattern(i))good=0;
    for(uint32_t i=0;i<2u*MIB;i++)if(room[i]!=(uint8_t)(pattern(i)^0xffu))good=0;
    check(good,"full6MiB byte verify plus headroom");
    check(!krealloc(big,16u*MIB),"realloc oversize");
    check(!krealloc(big,10u*MIB),"realloc real OOM cannot move past live headroom");
    good=1;for(uint32_t i=0;i<6u*MIB;i++)if(big[i]!=pattern(i))good=0;
    check(good,"failed realloc preserves full old allocation");
    kfree(big);kfree(room);kfree(NULL);
    uint8_t *a=kmalloc(4096),*b=kmalloc(4096),*c=kmalloc(4096);
    check(a && b && c,"split blocks");kfree(b);void *reuse=kmalloc(1024);
    check(reuse==b,"split free block reused");kfree(reuse);kfree(a);kfree(c);
    uint8_t *all=kmalloc(15u*MIB);check(all!=NULL,"coalesce recovered arena");
    check(!kmalloc(2u*MIB),"real arena OOM");all[0]=0x53;all[15u*MIB-1]=0x91;kfree(all);
    a=krealloc(NULL,128);check(a!=NULL,"realloc NULL");
    for(unsigned i=0;i<128;i++)a[i]=(uint8_t)i;
    b=krealloc(a,65536);check(b!=NULL,"realloc grow");
    good=1;for(unsigned i=0;i<128;i++)if(b[i]!=(uint8_t)i)good=0;check(good,"grow prefix");
    a=krealloc(b,32);check(a!=NULL,"realloc shrink");
    good=1;for(unsigned i=0;i<32;i++)if(a[i]!=(uint8_t)i)good=0;
    check(good && !krealloc(a,0),"shrink prefix and zero frees");
    check(!kcalloc(SIZE_MAX,2) && !kcalloc(0,16),"calloc overflow zero");
    a=kcalloc(8193,3);check(a!=NULL,"calloc spanning pages");
    good=1;for(unsigned i=0;i<8193u*3u;i++)if(a[i])good=0;check(good,"calloc full zero fill");kfree(a);
    all=kmalloc(15u*MIB);check(all!=NULL,"OOM recovery reusable");kfree(all);
    vga_write("ROUND2 PASS heap 16MiB 6MiB-full-byte headroom realloc calloc OOM\n");
}
static void failed_mmio(uint32_t phys,size_t size,const char *name){
    uint32_t before=pmm_free_frames(),h=page_hash();check(!paging_map_mmio(phys,size),name);
    check(pmm_free_frames()==before && page_hash()==h,"failed MMIO transactional");
}
static void test_mmio(void){
    failed_mmio(0x90000000,0,"MMIO zero");failed_mmio(0xfffff001,4096,"MMIO past4GiB");
    failed_mmio(0x90000000,SIZE_MAX,"MMIO overflow");failed_mmio(4u*MIB,4096,"MMIO RAM alias");
    failed_mmio(0xb8000,4096,"MMIO VGA alias");failed_mmio(0xa0000000,256u*MIB,"MMIO window capacity");
    volatile void *m=paging_map_mmio(0x90000123,5000);
    check(m && ((uintptr_t)m&4095)==0x123,"MMIO offset");uint32_t va=(uintptr_t)m&~4095u;
    check(va==0xe0000000u,"failed initial MMIO requests consume no window slots");
    check((pte(va)&~4095u)==0x90000000 && (pte(va+4096)&~4095u)==0x90001000,"MMIO rounded pages");
    check((pte(va)&0x9f)==0x1b && (pte(va+4096)&0x9f)==0x1b,"MMIO supervisor RW PCD PWT PAT clear");
    uint32_t before=pmm_free_frames(),h=page_hash();
    check(paging_map_mmio(0x90000123,5000)==m,"MMIO repeated reuse");
    check((uintptr_t)paging_map_mmio(0x90000080,16)==va+0x80,"MMIO covered rounded reuse");
    check(pmm_free_frames()==before && page_hash()==h,"MMIO reuse no resources");
    failed_mmio(0x90001000,8192,"MMIO partial overlap");
    volatile void *tail=paging_map_mmio(0xfffff000,4096);
    check(tail && (pte((uintptr_t)tail)&~4095u)==0xfffff000,"MMIO last physical page");
    check((uintptr_t)tail==va+8192,"overlap rejection consumes no window slots");
    unsigned count=0;
    while(count<2048){before=pmm_free_frames();h=page_hash();if(!paging_map_mmio(0xb0000000+count*8192,4096))break;count++;}
    check(count<2048,"finite MMIO descriptors exhausted");
    check(pmm_free_frames()==before && page_hash()==h,"capacity failure rollback");
    failed_mmio(0xc0000000,4096,"exhaustion repeat transactional");
    check(paging_map_mmio(0x90000123,5000)==m,"covered reuse after exhaustion");
    vga_write("ROUND2 PASS MMIO rounding flags aliases overflow reuse overlap capacity tail\n");
}
static __attribute__((noinline)) void stack_event(void){
    char a[]="stack actor",v[]="stack verb",o[]="stack object",r[]="stack result",n[]="stack note";
    wyrd_append(a,v,o,r,n);
    /* Volatile scrubs cannot become dead stores at -O2 when locals expire. */
    for(unsigned i=0;i<sizeof(a)-1;i++)((volatile char *)a)[i]='x';
    for(unsigned i=0;i<sizeof(v)-1;i++)((volatile char *)v)[i]='x';
    for(unsigned i=0;i<sizeof(o)-1;i++)((volatile char *)o)[i]='x';
    for(unsigned i=0;i<sizeof(r)-1;i++)((volatile char *)r)[i]='x';
    for(unsigned i=0;i<sizeof(n)-1;i++)((volatile char *)n)[i]='x';
}
static void test_timeline(void){
    WyrdEvent e,saved;wyrd_init();check(!wyrd_get(0,&e) && !wyrd_get(0,NULL),"empty copy out");stack_event();
    check(wyrd_get(0,&e) && !strcmp(e.actor,"stack actor") && !strcmp(e.verb,"stack verb") &&
        !strcmp(e.object,"stack object") && !strcmp(e.result,"stack result") && !strcmp(e.note,"stack note"),"five stack fields owned");
    char path[160];memset(path,'p',159);path[159]=0;wyrd_append("a","v",path,"r","n");path[0]='x';
    check(wyrd_get(1,&e) && strlen(e.object)==159 && e.object[0]=='p' && !e.truncated,"159-char path intact");
    int intact=1;for(unsigned i=0;i<159;i++)if(e.object[i]!='p')intact=0;
    check(intact,"159-char path every byte preserved");
    char longtext[256];memset(longtext,'L',255);longtext[255]=0;wyrd_append(longtext,"verb",longtext,"result","note");
    check(wyrd_get(2,&e) && e.truncated==(WYRD_TRUNC_ACTOR|WYRD_TRUNC_OBJECT) && strlen(e.actor)==63 && strlen(e.object)==159,"exact truncation metadata");
    captured=0;capture[0]=0;capturing=1;wyrd_render(1);capturing=0;
    check(contains(capture," [truncated:actor,object]"),"visible truncation marker");
    wyrd_append(longtext,longtext,longtext,longtext,longtext);
    check(wyrd_get(3,&e) && e.truncated==(WYRD_TRUNC_ACTOR|WYRD_TRUNC_VERB|WYRD_TRUNC_OBJECT|WYRD_TRUNC_RESULT|WYRD_TRUNC_NOTE) &&
        strlen(e.verb)==31 && strlen(e.result)==31 && strlen(e.note)==191,"all bounds metadata");
    saved=e;wyrd_init();for(unsigned i=0;i<WYRD_CAP+17;i++)wyrd_append("wrap","append","owned","ok","note");
    check(wyrd_count()==WYRD_CAP && wyrd_get(0,&e) && e.tick==18,"ring oldest wrap");
    check(wyrd_get(WYRD_CAP-1,&e) && e.tick==WYRD_CAP+17 && !wyrd_get(WYRD_CAP,&e),"ring newest bounds");
    check(strlen(saved.actor)==63 && saved.actor[0]=='L' && strlen(saved.note)==191,"snapshot survives overwrite");
    vga_write("ROUND2 PASS timeline owned-five-fields path159 truncation wrap snapshot\n");
}
static __attribute__((noreturn,noinline)) void fault(void){
    extern char round2_fault_instruction;
    vga_write("ROUND2 FAULT trigger CR2=0xDEADB000 expected-error=0x00000002 EIP=");
    vga_write_hex((uintptr_t)&round2_fault_instruction);vga_putc('\n');
    __asm__ volatile(".global round2_fault_instruction\nround2_fault_instruction:\nmovl $0x12345678,0xdeadb000":::"memory");
    vga_write("ROUND2 FAIL returned from fatal page fault\n");for(;;)__asm__ volatile("cli; hlt");
}
void kernel_main(uint32_t magic,uint32_t info_addr){
    (void)info_addr;wyrd_init();serial_init();vga_write("ROUND2 BOOT mode=");vga_write_dec(ROUND2_MODE);vga_putc('\n');
    check(magic==MULTIBOOT_BOOTLOADER_MAGIC,"Multiboot entry");gdt_init();idt_init();__asm__ volatile("cli");
    check(!paging_map_mmio(0x90000000,4096),"MMIO unavailable before paging init");
    fixture();paging_init();if(ROUND2_MODE==2)fault();
    test_ram();if(ROUND2_MODE==1){heap_init();fault();}
    test_heap();test_mmio();test_timeline();
    vga_write("ROUND2 PASS all assertions=");vga_write_dec(assertions);vga_putc('\n');
    outb(0xf4,0x10);for(;;)__asm__ volatile("cli; hlt");
}
