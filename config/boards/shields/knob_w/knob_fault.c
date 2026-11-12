#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#include <zephyr/sys/reboot.h>

/* Survives a software reboot, not a power cut or a bootloader RAM overwrite. */
static struct { uint32_t magic, reason, seconds, thread, pc, lr; } last_fault __noinit;

void knob_fault_read(uint32_t *reason, uint32_t *seconds) {
    bool valid=last_fault.magic==0x4b4e4643;
    *reason=valid?last_fault.reason:UINT32_MAX;
    *seconds=valid?last_fault.seconds:0;
}
uint32_t knob_fault_thread(void) {
    return last_fault.magic==0x4b4e4643?last_fault.thread:0;
}
uint32_t knob_fault_pc(void) { return last_fault.magic==0x4b4e4643?last_fault.pc:0; }
uint32_t knob_fault_lr(void) { return last_fault.magic==0x4b4e4643?last_fault.lr:0; }

void k_sys_fatal_error_handler(unsigned int reason,const struct arch_esf *esf) {
    last_fault.reason=reason;
    last_fault.seconds=k_uptime_get_32()/1000;
    last_fault.thread=(uint32_t)(uintptr_t)k_current_get();
    last_fault.pc=esf?esf->basic.pc:0;
    last_fault.lr=esf?esf->basic.lr:0;
    last_fault.magic=0x4b4e4643;
    sys_reboot(SYS_REBOOT_COLD);
    CODE_UNREACHABLE;
}
