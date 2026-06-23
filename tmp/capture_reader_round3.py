import serial, time, pathlib
port = 'COM9'
out_path = pathlib.Path('tmp/reader_round3_full_route_fix.txt')
ser = serial.Serial(port, 115200, timeout=0.2)
try:
    end = time.time() + 16
    chunks = []
    while time.time() < end:
        data = ser.read(4096)
        if data:
            chunks.append(data)
    out_path.write_bytes(b''.join(chunks))
    print(f'WROTE {out_path}')
finally:
    ser.close()
