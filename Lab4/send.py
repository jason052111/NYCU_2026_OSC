import serial
import os
import time
import struct 
import sys

def send_kernel():
    # Get the UART port from the command line argument, or use default '/dev/ttyUSB0'
    PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
    BAUD = 115200
    KERNEL_FILE = 'kernel/kernel.bin'
    MAGIC_NUMBER = 0x544F4F42  # "BOOT" ASCII (Little-Endian)

    # Check if the kernel file exists before proceeding
    if not os.path.exists(KERNEL_FILE):
        print(f"can't found {KERNEL_FILE}")
        return

    # Open the kernel file in binary read mode ('rb') and load all bytes into memory
    with open(KERNEL_FILE, 'rb') as f:
        kernel_data = f.read()

    kernel_size = len(kernel_data)
    print(f"--- prepare to transfer ---")
    print(f"file: {KERNEL_FILE}")
    print(f"size: {kernel_size} bytes (0x{kernel_size:x})")

    try:
        print(f"opening {PORT} ...")
        # Open the serial port with hardware/software flow control disabled
        with serial.Serial(PORT, BAUD, timeout=2, xonxoff=False, rtscts=False, dsrdtr=False) as ser:
            ser.reset_input_buffer()  # clean pipe
            ser.reset_output_buffer() # clean pipe
            # Pack the magic number and file size into an 8-byte Little-Endian binary header
            header = struct.pack('<II', MAGIC_NUMBER, kernel_size)
            print(f"transfer header (Magic: 0x{MAGIC_NUMBER:x}, Size: {kernel_size})...")
            # Send the header to wake up and sync with the C bootloader
            ser.write(header)
            time.sleep(0.1)
            print("starting transfer Kernel ...")
            chunk_size = 32 
            # Send kernel data 
        
        # 分塊傳送 kernel data
            for i in range(0, kernel_size, chunk_size):
                chunk = kernel_data[i : i + chunk_size]
                ser.write(chunk)
                ser.flush() # 確保資料立刻送出，而不是卡在 OS 的 buffer 裡
                time.sleep(0.01)
            
            # 顯示傳送進度 (覆蓋同一行印出)
                sent_bytes = min(i + chunk_size, kernel_size)
                sys.stdout.write(f"\rProgress: {sent_bytes}/{kernel_size} bytes")
                sys.stdout.flush()
            print("--- success transfer ---")

    except Exception as e:
        print(f"error: {e}")

if __name__ == '__main__':
    send_kernel()