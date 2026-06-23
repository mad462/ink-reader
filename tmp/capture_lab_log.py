import serial, time, pathlib
port = 'COM9'
out_path = pathlib.Path('tmp/lab_boot_capture.txt')
ser = serial.Serial(port, 115200, timeout=0.2)
try:
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.05)
    ser.setRTS(False)
    time.sleep(0.05)
    end = time.time() + 12
    chunks = []
    while time.time() < end:
        data = ser.read(4096)
        if data:
            chunks.append(data)
    out_path.write_bytes(b''.join(chunks))
    print(f'WROTE {out_path}')
finally:
    ser.close()
