import serial, time, pathlib
port = 'COM9'
out_path = pathlib.Path('tmp/reader_white_refresh_capture.txt')
ser = serial.Serial(port, 115200, timeout=0.2)
try:
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
