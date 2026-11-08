#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel.h"
#include "raven_ata.h"

/* Actual timer counter and driver; only port I/O and privileged instructions
 * are replaced. Simulated PIT IRQs make 31-second deadlines deterministic. */
#include "../kernel/timer.c"
static int if_enabled;
static unsigned halts, cli_count, sti_count, spurious, inject_before_cli;
static unsigned inject_pending, pending_irq;
static uint64_t elapsed_ticks;
static uint32_t ata_flags(void) { return if_enabled ? 0x200u : 0; }
static void tick(void) { timer_on_irq(); ++elapsed_ticks; }
static void ata_cli(void) {
    assert(if_enabled);
    if(inject_before_cli && elapsed_ticks==3099) { tick(); inject_before_cli=0; }
    if_enabled=0; ++cli_count;
}
static void ata_sti(void) {
    assert(!if_enabled); if_enabled=1; ++sti_count;
}
static void ata_sti_hlt(void) {
    assert(!if_enabled); if_enabled=1; ++halts;
    /* IRQ pending between the masked status/deadline check and STI/HLT
     * must wake HLT, not disappear in a check-then-sleep race. */
    if(inject_pending) { pending_irq=1; inject_pending=0; }
    if(spurious) { --spurious; return; }
    if(pending_irq) pending_irq=0;
    tick();
}
#define RAVEN_ATA_TEST
#include "../kernel/ata.c"

/* Port-level disk model is independent of the wait implementation. */
enum { DATA=0x1f0, COUNT=0x1f2, LBA0=0x1f3, LBA1=0x1f4,
       LBA2=0x1f5, SELECT=0x1f6, STATUS=0x1f7, ALT=0x3f6 };
enum { ID=0xec, READ=0x20, WRITE=0x30, FLUSH=0xe7 };
enum { OK, STUCK_BUSY, NO_DRQ, BUSY_DRQ, COMMAND_ERROR, COMMAND_FAULT,
       COMPLETE_ERROR, COMPLETE_BUSY, COMPLETE_DRQ, FLOATING };
typedef struct {
    uint8_t status, regs[4], command, head;
    uint16_t identify[256];
    uint8_t sector[512];
    unsigned commands[256], word, words_read, words_written, status_reads;
    unsigned busy_reads, command_busy_reads;
    int mode, flush_mode, signature;
    uint32_t last_lba, latency_ticks, command_start;
} MockDrive;
static MockDrive disks[2];
static unsigned selected, logs, slave_data_writes, cases;

static void init_disk(unsigned drive, uint32_t sectors) {
    MockDrive *d=&disks[drive];
    memset(d,0,sizeof(*d));
    d->status=0x40;
    d->identify[0]=0x40;
    d->identify[49]=1u<<9;
    d->identify[60]=(uint16_t)sectors;
    d->identify[61]=(uint16_t)(sectors>>16);
    for(unsigned i=0;i<512;i++) d->sector[i]=(uint8_t)(i*13+drive*37);
}
static void reset(void) {
    if_enabled=0; pit_ticks=0; elapsed_ticks=0;
    halts=cli_count=sti_count=spurious=inject_before_cli=0;
    inject_pending=pending_irq=0;
    init_disk(0,64); init_disk(1,2048);
    selected=logs=slave_data_writes=0;
    ata_init();
    assert(ata_present_flag() && ata_rw_ready_flag());
    assert(disks[0].commands[ID]==1 && disks[1].commands[ID]==0 && logs==1);
}
static uint8_t completion_status(int mode) {
    if(mode==COMPLETE_ERROR || mode==COMMAND_ERROR) return 0x41;
    if(mode==COMPLETE_BUSY || mode==STUCK_BUSY) return 0x80;
    if(mode==COMPLETE_DRQ) return 0x48;
    if(mode==COMMAND_FAULT) return 0x60;
    if(mode==FLOATING) return 0xff;
    return 0x40;
}
uint8_t inb(uint16_t port) {
    MockDrive *d=&disks[selected];
    if(port>=COUNT && port<=LBA2) return d->regs[port-COUNT];
    assert(port==STATUS || port==ALT);
    if(port==STATUS) {
        d->status_reads++;
        if(d->command && d->latency_ticks &&
           (uint32_t)(timer_ticks()-d->command_start)<d->latency_ticks)
            return 0x88;
        if(d->command && d->busy_reads) {
            d->busy_reads--;
            return 0x88; /* DRQ must not make BSY data readable. */
        }
    }
    return d->status;
}
void outb(uint16_t port,uint8_t value) {
    if(port==SELECT) {
        selected=(value>>4)&1; disks[selected].head=value; return;
    }
    MockDrive *d=&disks[selected];
    if(port>=COUNT && port<=LBA2) { d->regs[port-COUNT]=value; return; }
    assert(port==STATUS);
    assert(value==ID || value==READ || value==WRITE || value==FLUSH);
    d->commands[value]++; d->command=value; d->word=0;
    d->command_start=timer_ticks();
    d->busy_reads=d->command_busy_reads;
    d->last_lba=(uint32_t)d->regs[1] | ((uint32_t)d->regs[2]<<8) |
        ((uint32_t)d->regs[3]<<16) | ((uint32_t)(d->head&15)<<24);
    if(value==WRITE || value==FLUSH) assert(selected==0);
    if(value==FLUSH) { d->status=completion_status(d->flush_mode); return; }
    if(value==ID && d->signature) { d->regs[2]=0x14; d->regs[3]=0xeb; }
    switch(d->mode) {
    case STUCK_BUSY: d->status=0x80; break;
    case NO_DRQ: d->status=0x40; break;
    case BUSY_DRQ: d->status=0x88; break;
    case COMMAND_ERROR: d->status=0x41; break;
    case COMMAND_FAULT: d->status=0x60; break;
    case FLOATING: d->status=0xff; break;
    default: d->status=0x48; break;
    }
}
uint16_t inw(uint16_t port) {
    MockDrive *d=&disks[selected];
    assert(port==DATA && d->status==0x48 && !d->busy_reads);
    assert((d->command==ID || d->command==READ) && d->word<256);
    unsigned i=d->word++;
    uint16_t result=d->command==ID ? d->identify[i] :
        (uint16_t)((uint16_t)d->sector[i*2] | ((uint16_t)d->sector[i*2+1]<<8));
    d->words_read++;
    if(d->word==256) d->status=completion_status(d->mode);
    return result;
}
void outw(uint16_t port,uint16_t word) {
    MockDrive *d=&disks[selected];
    assert(port==DATA && d->command==WRITE && d->status==0x48);
    if(selected) slave_data_writes++;
    assert(!selected && d->word<256);
    d->sector[d->word*2]=(uint8_t)word;
    d->sector[d->word*2+1]=(uint8_t)(word>>8);
    d->word++; d->words_written++;
    if(d->word==256) d->status=completion_status(d->mode);
}
void wyrd_append(const char*a,const char*b,const char*c,const char*d,const char*e) {
    assert(a && b && c && d && e); logs++;
}
void vga_set_color(uint8_t a,uint8_t b) { (void)a; (void)b; }
void vga_write(const char*s) { (void)s; }
void vga_putc(char c) { (void)c; }
void vga_write_dec(uint32_t n) { (void)n; }

static void test_isolation(void) {
    reset();
    _Alignas(16) uint8_t buf[514];
    uint8_t original[512];
    memcpy(original,disks[0].sector,512);
    assert(raven_ata_sector_count(0)==64 && raven_ata_sector_count(1)==2048);
    assert(disks[1].commands[ID]==1);
    unsigned before=logs;
    memset(buf,0xa5,sizeof(buf));
    assert(raven_ata_read_sector(1,37,buf+1));
    assert(memcmp(buf+1,disks[1].sector,512)==0 && buf[0]==0xa5 && buf[513]==0xa5);
    assert(disks[1].last_lba==37 && disks[1].head==0xf0);
    assert(raven_ata_read_sector(1,1,buf));
    assert(logs==before && !slave_data_writes);
    assert(memcmp(original,disks[0].sector,512)==0);
    assert(!disks[0].commands[READ] && !disks[0].commands[WRITE]);
    assert(ata_read_sector(20,buf));
    assert(disks[0].last_lba==20 && disks[0].head==0xe0);
    assert(memcmp(buf,original,512)==0 && logs==before+1);
    cases++;
}
static void test_capacity_range(void) {
    reset(); uint8_t buf[512];
    assert(!raven_ata_read_sector(0,64,buf));
    assert(raven_ata_error(0)==RAVEN_ATA_ERROR_RANGE);
    assert(!ata_write_sector(64,buf) && !disks[0].commands[WRITE]);
    assert(!raven_ata_read_sector(0,0x10000000u,buf) && !disks[0].commands[READ]);
    init_disk(1,0xffffffffu);
    assert(raven_ata_probe(1) && raven_ata_sector_count(1)==0x10000000u);
    assert(raven_ata_read_sector(1,0x0fffffffu,buf));
    assert(disks[1].last_lba==0x0fffffffu && disks[1].head==0xff);
    unsigned before=disks[1].commands[READ];
    assert(!raven_ata_read_sector(1,0x10000000u,buf));
    assert(!raven_ata_read_sector(1,0xffffffffu,buf) && disks[1].commands[READ]==before);
    assert(!raven_ata_read_sector(1,0,NULL) && raven_ata_error(1)==RAVEN_ATA_ERROR_INVALID);
    assert(!raven_ata_probe(2) && !raven_ata_read_sector(2,0,buf));
    assert(!raven_ata_sector_count(2) && raven_ata_error(2)==RAVEN_ATA_ERROR_INVALID);
    cases++;
}
static void test_identify(void) {
    reset();
    for(unsigned i=0;i<5;i++) {
        init_disk(1,4096);
        if(i==0) disks[1].signature=1;
        if(i==1) disks[1].identify[0]=0x8000;
        if(i==2) disks[1].identify[49]=0;
        if(i==3) disks[1].identify[60]=disks[1].identify[61]=0;
        if(i==4) disks[1].mode=COMMAND_ERROR;
        assert(!raven_ata_probe(1) && raven_ata_error(1)==RAVEN_ATA_ERROR_IO);
        assert(raven_ata_sector_count(1)==0); cases++;
    }
    for(unsigned status=0;status<2;status++) {
        init_disk(1,4096); disks[1].status=status?0xff:0;
        assert(!raven_ata_probe(1) && raven_ata_error(1)==RAVEN_ATA_ERROR_ABSENT);
        assert(!raven_ata_sector_count(1) && !disks[1].commands[ID]); cases++;
    }
    init_disk(1,4096);
    assert(raven_ata_probe(1) && raven_ata_sector_count(1)==4096);
    assert(raven_ata_error(1)==RAVEN_ATA_ERROR_NONE); cases++;
}
static void test_recovery(void) {
    reset(); uint8_t buf[512];
    assert(raven_ata_probe(1)); disks[1].mode=COMMAND_ERROR;
    assert(!raven_ata_read_sector(1,0,buf) && raven_ata_error(1)==RAVEN_ATA_ERROR_IO);
    assert(selected==1 && disks[1].status==0x41);
    assert(ata_read_sector(20,buf)); /* Select before checking previous slave. */
    assert(selected==0 && raven_ata_error(0)==RAVEN_ATA_ERROR_NONE);
    assert(raven_ata_error(1)==RAVEN_ATA_ERROR_IO);
    disks[1].mode=OK; /* Latched ERR remains until command is issued. */
    assert(raven_ata_read_sector(1,0,buf) && raven_ata_error(1)==RAVEN_ATA_ERROR_NONE);
    disks[1].status=0x80;
    assert(ata_read_sector(20,buf));
    disks[0].mode=COMMAND_ERROR;
    assert(!ata_read_sector(20,buf) && disks[0].status==0x41);
    disks[0].mode=OK;
    assert(ata_read_sector(20,buf) && raven_ata_error(0)==RAVEN_ATA_ERROR_NONE);
    assert(ata_present_flag() && ata_rw_ready_flag()); cases++;
}
static void test_bounded_waits(void) {
    const int modes[]={STUCK_BUSY,NO_DRQ,BUSY_DRQ,COMPLETE_BUSY,COMPLETE_DRQ,
                       COMMAND_ERROR,COMMAND_FAULT,COMPLETE_ERROR,FLOATING};
    uint8_t buf[512];
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);i++) {
        reset(); assert(raven_ata_probe(1)); disks[1].mode=modes[i];
        unsigned before=disks[1].status_reads;
        assert(!raven_ata_read_sector(1,0,buf));
        int expected=modes[i]==FLOATING ? RAVEN_ATA_ERROR_ABSENT :
            (modes[i]==COMMAND_ERROR || modes[i]==COMMAND_FAULT || modes[i]==COMPLETE_ERROR) ?
            RAVEN_ATA_ERROR_IO : RAVEN_ATA_ERROR_TIMEOUT;
        assert(raven_ata_error(1)==expected);
        assert(disks[1].status_reads-before<=2*RAVEN_ATA_POLL_LIMIT+2);
        if(expected==RAVEN_ATA_ERROR_TIMEOUT)
            assert(disks[1].status_reads-before>=RAVEN_ATA_POLL_LIMIT);
        cases++;
    }
    reset(); disks[1].status=0x80;
    assert(!raven_ata_probe(1) && disks[1].status_reads==RAVEN_ATA_POLL_LIMIT);
    assert(raven_ata_error(1)==RAVEN_ATA_ERROR_TIMEOUT);
    reset(); assert(raven_ata_probe(1));
    disks[1].command_busy_reads=3;
    assert(raven_ata_read_sector(1,0,buf) && !disks[1].busy_reads);
    /* Success on the last permitted DRQ poll must not underflow a counter
     * and become either a false timeout or an unbounded loop. */
    disks[1].command_busy_reads=RAVEN_ATA_POLL_LIMIT-1;
    assert(raven_ata_read_sector(1,0,buf));
    disks[1].command_busy_reads=RAVEN_ATA_POLL_LIMIT;
    assert(!raven_ata_read_sector(1,0,buf));
    assert(raven_ata_error(1)==RAVEN_ATA_ERROR_TIMEOUT);
    cases+=4;
}
static void test_master_write_completion(void) {
    _Alignas(16) uint8_t buf[513];
    for(unsigned i=0;i<sizeof(buf);i++) buf[i]=(uint8_t)(i*11);
    reset(); assert(raven_ata_probe(1));
    uint8_t slave[512]; memcpy(slave,disks[1].sector,512);
    assert(ata_write_sector(20,buf+1));
    assert(disks[0].commands[WRITE]==1 && disks[0].commands[FLUSH]==1);
    assert(disks[0].words_written==256 && memcmp(disks[0].sector,buf+1,512)==0);
    assert(memcmp(disks[1].sector,slave,512)==0 && !slave_data_writes);
    assert(!disks[1].commands[WRITE] && !disks[1].commands[FLUSH]); cases++;
    const int modes[]={COMPLETE_ERROR,COMPLETE_BUSY,COMPLETE_DRQ};
    for(unsigned i=0;i<3;i++) {
        reset(); disks[0].mode=modes[i];
        assert(!ata_write_sector(20,buf) && !disks[0].commands[FLUSH]);
        assert(raven_ata_error(0)==(i?RAVEN_ATA_ERROR_TIMEOUT:RAVEN_ATA_ERROR_IO)); cases++;
    }
    const int flush_modes[]={COMMAND_ERROR,COMMAND_FAULT,STUCK_BUSY,COMPLETE_DRQ,FLOATING};
    for(unsigned i=0;i<sizeof(flush_modes)/sizeof(flush_modes[0]);i++) {
        reset(); disks[0].flush_mode=flush_modes[i];
        assert(!ata_write_sector(20,buf) && disks[0].commands[FLUSH]==1);
        assert(raven_ata_error(0)!=RAVEN_ATA_ERROR_NONE); cases++;
    }
    reset(); disks[0].flush_mode=COMMAND_ERROR;
    assert(!ata_write_sector(20,buf));
    disks[0].flush_mode=OK;
    assert(ata_write_sector(20,buf)); /* Own stale flush ERR is recoverable. */
    assert(raven_ata_error(0)==RAVEN_ATA_ERROR_NONE); cases++;
}
static void test_timed_waits(void) {
    const uint32_t starts[]={0,UINT32_MAX-20};
    uint8_t buf[512];
    for(unsigned i=0;i<sizeof(starts)/sizeof(starts[0]);i++) {
        reset(); assert(raven_ata_probe(1));
        if_enabled=1; pit_ticks=starts[i];
        disks[1].mode=STUCK_BUSY;
        /* Non-timer wakes must not spend the elapsed-time budget. */
        spurious=7; inject_pending=1;
        unsigned before=disks[1].status_reads;
        assert(!raven_ata_read_sector(1,0,buf));
        assert(raven_ata_error(1)==RAVEN_ATA_ERROR_TIMEOUT);
        assert(elapsed_ticks==3100 && (uint32_t)(timer_ticks()-starts[i])==3100);
        assert(halts==3107 && !pending_irq && if_enabled);
        assert(disks[1].status_reads-before>RAVEN_ATA_POLL_LIMIT);
        assert(cli_count==halts+1 && sti_count==1);
        /* Same device recovers after timeout, now taking 1500 ms. No stale
         * timeout, poll budget or IF state may poison the subsequent read. */
        disks[1].status=0x40; disks[1].mode=OK; disks[1].command=0;
        disks[1].latency_ticks=150;
        uint64_t before_ticks=elapsed_ticks;
        assert(raven_ata_read_sector(1,0,buf));
        assert(elapsed_ticks-before_ticks==150 && if_enabled);
        assert(raven_ata_error(1)==RAVEN_ATA_ERROR_NONE);
        assert(memcmp(buf,disks[1].sector,512)==0);
        cases+=2;
    }
    /* Every phase, not just READ's DRQ, obeys the real deadline. */
    for(int phase=WAIT_PRECOMMAND;phase<=WAIT_IDENTIFY;phase++) {
        reset(); if_enabled=1; selected=1; disks[1].status=0x80;
        assert(!wait_status(1,phase));
        assert(elapsed_ticks==3100 && halts==3100 && if_enabled);
        assert(raven_ata_error(1)==RAVEN_ATA_ERROR_TIMEOUT); cases++;
    }
    /* Deadline tick arrives immediately before CLI. The masked recheck must
     * observe it rather than sleeping for one more tick. */
    reset(); if_enabled=1; selected=1; disks[1].status=0x80;
    inject_before_cli=1;
    assert(!wait_status(1,WAIT_DRQ));
    assert(elapsed_ticks==3100 && halts==3099 && if_enabled && !inject_before_cli); cases++;
    /* A slow command without BSY, or with DRQ stuck during completion, has
     * the same elapsed-time bound. Immediate ERR/DF remain immediate. */
    const int modes[]={NO_DRQ,BUSY_DRQ,COMPLETE_BUSY,COMPLETE_DRQ};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);i++) {
        reset(); assert(raven_ata_probe(1)); if_enabled=1;
        disks[1].mode=modes[i];
        assert(!raven_ata_read_sector(1,0,buf));
        assert(raven_ata_error(1)==RAVEN_ATA_ERROR_TIMEOUT);
        assert(elapsed_ticks==3100 && halts==3100 && if_enabled); cases++;
    }
    reset(); assert(raven_ata_probe(1)); if_enabled=1;
    disks[1].command_busy_reads=3;
    assert(raven_ata_read_sector(1,0,buf));
    assert(!halts && !cli_count && !elapsed_ticks && if_enabled); cases++;
    /* Immediate success, absence, and I/O failure all preserve enabled IF
     * without unnecessary sleep, including zero and floating status. */
    const uint8_t statuses[]={0x48,0,0xff,0x41,0x60};
    for(unsigned i=0;i<sizeof(statuses);i++) {
        reset(); if_enabled=1; selected=1; disks[1].status=statuses[i];
        assert(wait_status(1,WAIT_DRQ)==(i==0));
        assert(!halts && !cli_count && !elapsed_ticks && if_enabled);
        if(i) assert(raven_ata_error(1)==(i<3?RAVEN_ATA_ERROR_ABSENT:RAVEN_ATA_ERROR_IO));
        cases++;
    }
    /* Success and errors discovered after sleeping also restore IF. */
    for(unsigned i=0;i<sizeof(statuses);i++) {
        reset(); if_enabled=1; selected=1;
        disks[1].command=READ; disks[1].latency_ticks=150;
        disks[1].status=statuses[i];
        assert(wait_status(1,WAIT_DRQ)==(i==0));
        assert(elapsed_ticks==150 && halts==150 && if_enabled && sti_count==1);
        if(i) assert(raven_ata_error(1)==(i<3?RAVEN_ATA_ERROR_ABSENT:RAVEN_ATA_ERROR_IO));
        cases++;
    }
    /* A response just under the generous deadline is not a spin timeout. */
    reset(); assert(raven_ata_probe(1)); if_enabled=1;
    disks[1].command=0;
    disks[1].latency_ticks=3099;
    assert(raven_ata_read_sector(1,0,buf) && elapsed_ticks==3099 && if_enabled); cases++;
    /* IF=0 cannot advance the clock and must never STI or HLT, even for a
     * device which would complete after time passes with IF enabled. */
    reset(); assert(raven_ata_probe(1)); disks[1].latency_ticks=150;
    assert(!raven_ata_read_sector(1,0,buf));
    assert(!elapsed_ticks && !halts && !cli_count && !sti_count && !if_enabled);
    assert(raven_ata_error(1)==RAVEN_ATA_ERROR_TIMEOUT); cases++;
}
int main(void) {
    test_isolation(); test_capacity_range(); test_identify(); test_recovery();
    test_bounded_waits(); test_master_write_completion(); test_timed_waits();
    printf("ATA port-level tests: %u cases passed\n",cases);
    return 0;
}
