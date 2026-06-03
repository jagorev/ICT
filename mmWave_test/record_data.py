import serial
import time

print("Connecting to ESP32-S3 on COM7...")

try:
    esp32 = serial.Serial()
    esp32.port = 'COM7'
    esp32.baudrate = 115200
    esp32.timeout = 1  
    
    esp32.open()
    esp32.setDTR(True)
    esp32.setRTS(False)
    
    print("Connected successfully!")
    print("Recording data. Press Ctrl+C to stop...\n")
    print("-" * 75)

    with open('radar_data.csv', 'w') as file:
        # Write CSV Headers
        file.write("Presence,Distance_m,ActiveGate,MaxEnergy\n")
        
        while True:
            line_bytes = esp32.readline()
            if not line_bytes:
                continue
                
            line = line_bytes.decode('utf-8', errors='ignore').strip()
            if not line or "ESP32" in line:
                continue

            # 1. SAVE RAW DATA TO CSV
            file.write(line + '\n')
            file.flush() 

            # 2. PRINT VISUAL DASHBOARD
            parts = line.split(',')
            
            if len(parts) == 4:
                presence_val = parts[0]
                distance = parts[1]
                gate = parts[2]
                energy = parts[3]
                
                presence_str = "YES 🔴" if presence_val == '1' else "NO  ⚪"
                    
                print(f"Presence: {presence_str} | Target Dist: {distance}m | Highest Energy: {energy:<5} (at Gate {gate})")
            else:
                print(f"> {line}")

except serial.SerialException as e:
    print(f"\nSerial Error: {e}")
    print("TIP: Is the PlatformIO terminal definitely closed?")
except KeyboardInterrupt:
    print("\n" + "-" * 75)
    print("Recording stopped safely.")
finally:
    if 'esp32' in locals() and esp32.is_open:
        esp32.close()