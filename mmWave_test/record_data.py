import serial
import time

print("Connecting to ESP32-S3 on COM7...")

try:
    esp32 = serial.Serial()
    esp32.port = 'COM7'
    esp32.baudrate = 115200
    
    # CRITICAL FIX 1: Add a 1-second timeout. 
    # This prevents Python from waiting forever and freezing your terminal!
    esp32.timeout = 1  
    
    esp32.open()
    
    # CRITICAL FIX 2: Tell the ESP32 Native USB that a terminal is open and ready.
    # Without this, the ESP32-S3 won't send any data over the USB-C cable.
    esp32.setDTR(True)
    esp32.setRTS(False)
    
    print("Connected successfully!")
    print("Recording data. Press Ctrl+C to stop...\n")
    print("-" * 55)

    with open('radar_data.csv', 'w') as file:
        while True:
            # Read the line (will give up and move on after 1 second if empty)
            line_bytes = esp32.readline()
            
            # If no data came through in the last second, just loop again
            if not line_bytes:
                continue
                
            line = line_bytes.decode('utf-8', errors='ignore').strip()
            
            if not line:
                continue

            # 1. SAVE TO FILE
            file.write(line + '\n')
            file.flush() 

            # 2. PRINT TO TERMINAL
            parts = line.split(',')
            
            if len(parts) == 3 and parts[0].isdigit():
                macro = parts[0]
                micro = parts[1]
                
                if parts[2] == '1':
                    presence = "YES 🔴"
                else:
                    presence = "NO  ⚪"
                    
                print(f"Macro Energy: {macro:<5} | Micro Energy: {micro:<5} | Presence: {presence}")
            else:
                print(f"> {line}")

except serial.SerialException as e:
    print(f"\nSerial Error: {e}")
    print("TIP: Make sure the PlatformIO Serial Monitor is completely closed!")
except KeyboardInterrupt:
    print("\n" + "-" * 55)
    print("Recording stopped safely. Your 'radar_data.csv' file is ready!")
finally:
    if 'esp32' in locals() and esp32.is_open:
        esp32.close()