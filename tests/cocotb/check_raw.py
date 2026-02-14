import struct
import os

raw_path = '../../linux_software/image_converter/image.raw'
if not os.path.exists(raw_path):
    raw_path = 'linux_software/image_converter/image.raw' # alternate from root

with open(raw_path, 'rb') as f:
    data = f.read()
    words = struct.unpack('<' + str(len(data)//4) + 'I', data)
    print(f"Total Words: {len(words)}")
    print(f"P0: {words[0]:06X}")
    print(f"P1279: {words[1279]:06X}")
    print(f"P1280: {words[1280]:06X}")
    print(f"P1281: {words[1281]:06X}")
    # Also check start of some later lines
    print(f"P2560 (Line 2 Start): {words[2560]:06X}")
