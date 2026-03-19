import serial, csv

SERIAL_PORT = 'COM7'
SERIAL_RATE = 9600

def main():
    ser = serial.Serial(SERIAL_PORT, SERIAL_RATE, timeout=2)

    with open("noise.csv", "a", newline="") as f:
        writer = csv.writer(f)
        while True:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            # ensure it's an integer line
            try:
                val = int(line)
            except ValueError:
                continue
            print(val)
            writer.writerow([val])
            f.flush()

if __name__ == "__main__":
    main()