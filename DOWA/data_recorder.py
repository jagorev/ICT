import os
import serial
import csv
from datetime import datetime

# --- CONFIGURATION ---
SERIAL_PORT = 'COM7'  # <-- Double check this!
BAUD_RATE = 115200

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CSV_FILENAME = os.path.join(SCRIPT_DIR, 'ml_training_dataset.csv')

print(f"Connecting to {SERIAL_PORT}...")
print(f"File will be saved to: {CSV_FILENAME}")
print("Using mmWave as implicit Ground Truth. Recording ML data...\nPress Ctrl+C to stop.\n")

try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)

    with open(CSV_FILENAME, mode='a', newline='') as file:
        writer = csv.writer(file)
        
        # Write headers if the file is new
        if os.stat(CSV_FILENAME).st_size == 0:
            writer.writerow(["Timestamp", "mmWave_State", "mmWave_Distance_m", "SGP30_TVOC_ppb", "SGP30_eCO2_ppm", "PIR_State", "Ground_Truth"])
            file.flush()

        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8').strip()

                if " | CSV:" in line:
                    dashboard_text, csv_data = line.split(" | CSV:")
                    print(dashboard_text)

                    # Now parsing 5 variables!
                    data = csv_data.split(',')
                    if len(data) == 5:
                        state = data[0].strip()
                        distance = data[1].strip()
                        tvoc = data[2].strip()
                        eco2 = data[3].strip()
                        pir = data[4].strip()  # <--- NEW PIR DATA
                        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

                        # Save everything + Ground Truth
                        writer.writerow([timestamp, state, distance, tvoc, eco2, pir, state])  # Using mmWave state as implicit Ground Truth
                        file.flush()
                else:
                    print(line)

except serial.SerialException as e:
    print(f"\nError connecting to {SERIAL_PORT}: {e}")
except KeyboardInterrupt:
    print("\nRecording stopped by user. Data saved successfully.")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()