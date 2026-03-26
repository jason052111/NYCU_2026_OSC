.section ".text.boot"           // Place this startup code in the .text.boot section
.global _start                  // Export _start symbol so the linker/bootloader can find the program entry point
_start:
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