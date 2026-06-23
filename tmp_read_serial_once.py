import serial, time
ser = serial.Serial('COM9', 115200, timeout=0.2)
end = time.time() + 8
chunks = []
while time.time() < end:
    data = ser.read(4096)
    if data:
        chunks.append(data.decode('utf-8', errors='replace'))
ser.close()
print(''.join(chunks))
