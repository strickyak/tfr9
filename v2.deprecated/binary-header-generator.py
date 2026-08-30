import sys

if len(sys.argv) < 2:
    raise Exception('filename argument(s) required')

for filename in sys.argv[1:]:
    for b in open(filename, "rb").read():
        print("  0x%02x," % b)
