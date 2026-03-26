OUTPUT_ARCH(riscv) # This output file is for the RISC-V architecture 
ENTRY(_start)      # Set entry point, where the program start to execute first instruction 
SECTIONS           # Define how each section should be arranged in memory
{
    . = 0x00200000;                     # Set the current output address to 0x00200000 (must match the bootloader/FIT load & entry address)
    _start = .;                         # Define the symbol _start at the current address
    .text : { *(.text.boot) *(.text) }  # Place all code sections here, boot code in .text.boot first, then normal .text
    .rodata : { *(.rodata) }            # Place all read-only data here
    .data : { *(.data) }                # Place all initialized global/static variables here
    .bss : {                            # Place all zero-initialized global/static variables here
        __bss_start = .;                # Mark the start address of the .bss region
        *(.sbss) *(.bss)                # Collect small bss and normal bss from all object files
        __bss_stop = .;                 # Mark the end address of the .bss region
    }
    . = ALIGN(16);                      # Align the current address to a 16-byte boundary
    . = . + 0x4000;                     # Reserve 0x4000 bytes for the stack space
    _end = .;                           # Define _end at the top of the reserved stack area
}