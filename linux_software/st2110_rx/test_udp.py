import socket
import time

print("Sending test packet to 127.0.0.1:5000...")
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.sendto(b"X" * 1460, ("127.0.0.1", 5000))
print("Done.")
