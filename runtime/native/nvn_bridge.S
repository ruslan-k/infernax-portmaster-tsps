/* Normal-context ABI boundary retained from approved CotM production.
 * SDK4.4.1 dispatch address is derived from pinned Mega Man image metadata.
 */
#include "megaman_graphics_contract.h"
.text
.align 2
.macro nvn_veneer name,id
.global cotm_\name
.type cotm_\name,%function
cotm_\name:
    mov x16, #\id
    b cotm_nvn_entry
.size cotm_\name,.-cotm_\name
.endm
#define NVN_ENTRY(name,id) nvn_veneer name,id
#include "nvn_entries.inc"
#undef NVN_ENTRY
.type cotm_nvn_entry,%function
cotm_nvn_entry:
    sub sp, sp, #256
    stp x0, x1, [sp]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x18, [sp, #64]
    stp x29, x30, [sp, #80]
    add x9, sp, #256
    str x9, [sp, #96]
    stp q0, q1, [sp, #112]
    stp q2, q3, [sp, #144]
    stp q4, q5, [sp, #176]
    stp q6, q7, [sp, #208]
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [sp, #240]
    mov x29, sp
    mov x0, sp
    mov w1, w16
    bl cotm_nvn_dispatch
    ldp x9, x10, [sp, #240]
    msr fpcr, x9
    msr fpsr, x10
    ldr q0, [sp, #112]
    ldr x18, [sp, #72]
    ldp x29, x30, [sp, #80]
    add sp, sp, #256
    ret
.size cotm_nvn_entry,.-cotm_nvn_entry

.global cotm_nvn_original_lookup
.type cotm_nvn_original_lookup,%function
cotm_nvn_original_lookup:
    stp x18, x30, [sp, #-16]!
    mov x18, x2
    // Animal Well SDK 17.5.3: the exported lookup object holds a thunk (trapped for the guest);
    // the host calls the real nvnDeviceGetProcAddress whose pointer the sdk keeps at this slot.
    ldr x8, =LC2_NVN_LOOKUP_POINTER
    ldr x9, [x8]
    blr x9
    ldp x18, x30, [sp], #16
    ret
.size cotm_nvn_original_lookup,.-cotm_nvn_original_lookup

.global cotm_nvn_call_original
.type cotm_nvn_call_original,%function
cotm_nvn_call_original:
    stp x19, x20, [sp, #-32]!
    stp x18, x30, [sp, #16]
    mov x20, x0
    mov x19, x1
    ldp x0, x1, [x19]
    ldp x2, x3, [x19, #16]
    ldp x4, x5, [x19, #32]
    ldp x6, x7, [x19, #48]
    ldp x8, x18, [x19, #64]
    ldp q0, q1, [x19, #112]
    ldp q2, q3, [x19, #144]
    ldp q4, q5, [x19, #176]
    ldp q6, q7, [x19, #208]
    blr x20
    str q0, [x19, #112]
    ldp x18, x30, [sp, #16]
    ldp x19, x20, [sp], #32
    ret
.size cotm_nvn_call_original,.-cotm_nvn_call_original
.section .note.GNU-stack,"",%progbits
