.section ".text.boot"           // Place this startup code in the .text.boot section
.global _start                  // Export _start symbol so the linker/bootloader can find the program entry point
_start:
    auipc s0, 0                 // Get current PC and store in s0
    ld s1, target_start         // Load the linked base address (_start's expected address) into s1
    beq s0, s1, clear           // If current address == expected address, skip relocation
    ld s2, target_bss_start     // Load the linked address of __bss_start into s2
    sub t0, s2, s1              // Calculate code + data size (t0 = bss_start - start)
    add t0, s0, t0              // Calculate the source end boundary (t0 = current_pc + size)

copy_loop:
    bge s0, t0, copy_done       // If source pointer s0 >= end boundary t0, copy is finished
    ld t1, 0(s0)                // Load 8 bytes from the source address
    sd t1, 0(s1)                // Store 8 bytes from the source address
    addi s0, s0, 8              // Increment source pointer by 8 bytes
    addi s1, s1, 8              // Increment target pointer by 8 bytes
    j copy_loop                 // Repeat the copy process

copy_done:
    ld t0, target_start         // Reload the intended start address
    jr t0                       // Jump to the new location

clear:
    la a3, __bss_start          // Load address of __bss_start into a3
    la a4, __bss_stop           // Load address of __bss_stop into a4
    ble a4, a3, clear_bss_done  // If __bss_stop <= __bss_start, .bss is empty; skip clearing.
clear_bss:
    sd zero, (a3)               // Store 0 to current .bss address, clearing one 64-bit word
    add a3, a3, 8               // Move to the next 8-byte slot in .bss
    blt a3, a4, clear_bss       // Loop until a3 reaches __bss_stop
clear_bss_done:            
    la sp, _end                 // Initialize stack pointer to _end
    tail start_kernel           // Jump to start_kernel() without returning

.align 3                        // Align to 8-byte boundary
// The following labels store the ABSOLUTE ADDRESSES defined at link-time.
target_start:      .dword _start
target_bss_start:  .dword __bss_start
