# NYCU OSC 2026 - Lab 2: Booting

This repository contains the implementation of a custom bare-metal bootloader for the RISC-V architecture (supporting OrangePi RV2 and QEMU virt machine).

## 📝 About The Project

The primary goal of this lab is to build a bootloader from scratch that initializes the system environment and loads the operating system kernel. Since this is a bare-metal environment, standard C libraries (`<string.h>`, `<stdlib.h>`) are not available, requiring custom implementations for memory manipulation, string parsing, and hardware interaction.

### Key Capabilities:
* **UART Booting:** Dynamically loads the kernel image via serial communication, accelerating the development cycle.
* **Hardware Abstraction:** Parses the Flattened Device Tree (FDT) to discover hardware addresses dynamically.
* **Initial Ramdisk (Initrd):** Parses a CPIO archive to provide a basic shell with file system commands (`ls`, `cat`).
* **Self-Relocation:** The bootloader can relocate itself in memory to free up the standard entry point for the kernel.

---

## 🚀 Features & Implementation Details

### 🟢 1. UART Bootloader (Basic Exercise 1)
To avoid frequently moving the SD card between the host and the board during kernel debugging, a UART-based boot mechanism was implemented.
* **Custom Transfer Protocol:** Uses a Python script (`send.py`) to send an 8-byte header containing a Magic Number (`"BOOT"` / `0x544F4F42`) and the kernel size, followed by the raw binary payload.
* **Reliable Binary Reception:** Implemented `uart_getb()` using a polling mechanism on the UART LSR register to receive raw bytes without corrupting data (avoiding `\n` or `\r` conversions).
* **Cache Synchronization:** Utilizes the RISC-V `fence.i` instruction before jumping to the newly loaded kernel to ensure the Instruction Cache (I-Cache) and Data Cache (D-Cache) are perfectly synchronized.

### 🟢 2. Devicetree Parsing (Basic Exercise 2)
To maintain portability, hardware addresses are dynamically queried from the `.dtb` file instead of being hardcoded.
* **Dynamic Node Traversal:** The `get_address()` function traverses the FDT structure block to find specific nodes (e.g., `/soc/serial` or `/soc/uart`) and extract their memory base addresses.
* **Endianness & Alignment Handling:** Implemented `bswap32()` to convert FDT's Big-Endian format to RISC-V's Little-Endian. A bitwise `align_up()` function ensures strict 4-byte boundary alignment while traversing the FDT tokens, preventing memory access faults.

### 🟢 3. Initial Ramdisk / CPIO (Basic Exercise 3)
Implemented a parser for the "New ASCII Format Cpio Archive" to load user programs before a full file system is available.
* **Dynamic Loading:** The initrd physical address is queried dynamically from the `/chosen` node's `linux,initrd-start` property in the devicetree.
* **CPIO Parsing:** Safely converts fixed-length ASCII hexadecimal strings (like `c_filesize`) into integers using a custom `hextoi()` function. It meticulously handles the padding requirements of the CPIO specification to maintain 4-byte alignment.
* **Shell Commands:** Supports `ls` (iterating through headers to list files) and `cat` (searching and printing file payloads to UART).

### 🔴 4. Bootloader Self-Relocation (Advanced Exercise)
To allow the loaded kernel to execute at the standard entry point (e.g., `0x00200000` on OrangePi RV2), the bootloader must move out of the way.
* **Reality PC via Inline Assembly:** Uses RISC-V inline assembly (`auipc %0, 0`) to determine the bootloader's current execution address.
* **Relocation Logic:** Calculates the offset between the ideal Linker address (`_start`) and the reality PC. It then copies itself to a higher, safer memory region (e.g., `0x20000000`), freeing up the standard entry point for the incoming kernel.

---

## 🌳 Flattened Device Tree (FDT)

In a bare-metal environment or during early OS boot stages, the system uses a Device Tree Blob (DTB) to dynamically discover hardware configurations. This eliminates the need to hardcode physical memory addresses into the kernel source code.

The DTB is stored in memory as a **Flattened Device Tree (FDT)**, which consists of a single contiguous memory region divided into four distinct blocks:

1. **FDT Header**
2. **Memory Reservation Block**
3. **Structure Block**
4. **Strings Block**

---

### 1. FDT Header
The header acts as the entry point, describing the total size of the device tree and the memory offsets to the other three blocks.

> **⚠️ Endianness Warning:** The FDT specification strictly mandates that all integer fields are stored in **Big-Endian** format. When running on a Little-Endian architecture (like RISC-V), values must be byte-swapped (e.g., using `bswap32`) before being interpreted.

```c
struct fdt_header {
    uint32_t magic;             // Magic number (must be 0xd00dfeed)
    uint32_t totalsize;         // Total size of the FDT block in bytes
    uint32_t off_dt_struct;     // Offset to the Structure Block
    uint32_t off_dt_strings;    // Offset to the Strings Block
    uint32_t off_mem_rsvmap;    // Offset to the Memory Reservation Block
    uint32_t version;           // FDT format version (typically 17)
    uint32_t last_comp_version; // Lowest compatible version
    uint32_t boot_cpuid_phys;   // Physical CPU ID of the booting hart
    uint32_t size_dt_strings;   // Size of the Strings Block
    uint32_t size_dt_struct;    // Size of the Structure Block
};
```

### 2. Memory Reservation Block
This block contains a list of physical memory regions that are strictly reserved and must not be used by the OS's general memory allocator (e.g., regions holding the bootloader or secure monitor).

* **Format:** A sequence of 64-bit physical addresses paired with 64-bit sizes.
* **Termination:** The list is terminated by a pair where both the address and size are `0x00000000_00000000`.

### 3. Structure Block
This is the core of the FDT. It contains all the device nodes and properties, flattened into a linear sequence of tokens.

> **⚠️ Alignment Requirement:** Every token and its associated data in the Structure Block must be strictly aligned to a **4-byte boundary**. If a string or property data length is not a multiple of 4, it is padded with null bytes (`\0`).

The structure block is composed of the following 32-bit tokens:

| Token Tag | Hex Value | Description & Behavior |
| :--- | :--- | :--- |
| `FDT_BEGIN_NODE` | `0x1` | Marks the start of a node. It is immediately followed by the node's name as a null-terminated string, and then padded to a 4-byte boundary. |
| `FDT_END_NODE` | `0x2` | Marks the end of the current node. It carries no extra data. |
| `FDT_PROP` | `0x3` | Represents a property. It is followed by two 32-bit integers: `len` (data length) and `nameoff` (offset in the Strings Block). The actual property data follows these integers, padded to 4 bytes. |
| `FDT_NOP` | `0x4` | A no-operation token. The parser should simply ignore it and move to the next 32-bit token. |
| `FDT_END` | `0x9` | Marks the absolute end of the Structure Block. |

**Parsing Logic:** Properties (`FDT_PROP`) belonging to a node appear sequentially after the `FDT_BEGIN_NODE` token. Sub-nodes are represented by nested `FDT_BEGIN_NODE` tags before the parent node's `FDT_END_NODE` is reached.

### 4. Strings Block
In a device tree, many property names (like `reg`, `compatible`, or `status`) appear repeatedly across different nodes. To optimize memory usage, the FDT extracts all property names and stores them in this centralized Strings Block.

* **Format:** A series of null-terminated strings packed tightly together.
* **Resolution:** When the parser encounters an `FDT_PROP` token in the Structure Block, it reads the `nameoff` integer. The property name can then be retrieved via a pointer calculated as: `(Strings_Block_Base_Address + nameoff)`.

---

## 📦 Initial Ramdisk (CPIO)

To load user programs and files before a full file system is implemented, this project utilizes an Initial Ramdisk (Initrd) packed in the **New ASCII Format Cpio Archive (`newc`)**.

### CPIO `newc` Format Mechanisms
The CPIO archive is a concatenated sequence of files, each starting with a header followed by the file's pathname and the actual file data.

```c
struct cpio_t {
    char magic[6];      // Magic number: "070701"
    char ino[8];
    char mode[8];
    // ... [other 8-byte ASCII hex fields] ...
    char filesize[8];   // Size of the file data in bytes
    // ...
    char namesize[8];   // Size of the pathname (including null byte)
    char check[8];      // Checksum
};
```

### Parsing Challenges & Solutions
1. **ASCII Hexadecimal Strings vs. Integers:** The fields inside the CPIO header (like `filesize` and `namesize`) are **not** raw binary integers. They are fixed-length (8 bytes) ASCII strings representing hexadecimal values. A custom `hextoi()` function is required to convert these ASCII strings (e.g., `"0000001A"`) into actual integers (e.g., `26`) for pointer arithmetic.
2. **Variable Length & Padding:**
   The CPIO format enforces strict memory alignment rules:
   * The pathname is followed by a `\0` byte.
   * The **pathname**  must be padded with `\0` to align on a **4-byte boundary**.
   * A bitwise `align_up()` operation is used extensively to correctly calculate the offset of the next file's `070701` magic number without reading garbage data.
3. **End of Archive:**
   The end of the CPIO archive is explicitly marked by a special file named `"TRAILER!!!"`. The parser loops until it matches this specific filename.
