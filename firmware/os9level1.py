import machine
# import usb_serial_example

RESET_VECTOR = 0xF068

Vectors = [
     RESET_VECTOR, # 6309 ERROR
     0x00DF, # SWI3
     0x00E5, # SWI2
     0x00EF, # FIRQ
     0x00F0, # IRQ
     0x00F4, # SWI
     0x00EB, # NMI
     RESET_VECTOR, # RESET
]

Ram = bytearray(2**16)
RomFd = open("boot.rom", "rb")
Rom = RomFd.read()
print("Rom size %d" % len(Rom))
RomFd.close()
del RomFd
for i in range(len(Rom)):
    Ram[0xC000 + i] = Rom[i]
del Rom
for i in range(len(Vectors)):
    Ram[0xFFF0 + 2*i + 0] = 255&(Vectors[i] >> 8)
    Ram[0xFFF0 + 2*i + 1] = 255&(Vectors[i])

Led = machine.Pin("LED", machine.Pin.OUT)

for y in [ x for x in range(23) ] + [26, 27, 28]:
    machine.Pin(y, machine.Pin.IN, machine.Pin.PULL_UP)

Ebar = machine.Pin(16, machine.Pin.OUT)
Qbar = machine.Pin(17, machine.Pin.OUT)
M0 = machine.Pin(18, machine.Pin.OUT)
M1 = machine.Pin(19, machine.Pin.OUT)
M2 = machine.Pin(20, machine.Pin.OUT)

for y in range(16, 21):
    p = machine.Pin(y, machine.Pin.OUT)
    p.value(1)

def OutLowDataByte(x):
    for i in range(8):
        p = machine.Pin(i, machine.Pin.OUT)
        p.value(1 & x)
        x = x >> 1

def Latch(x):
    print("Latch: %02x == %s %s %s %s %s %s" % (
        x,
        "-" if x & 0x01 else "RESET",
        "-" if x & 0x02 else "HALT",
        "-" if x & 0x04 else "IRQ",
        "-" if x & 0x08 else "FIRQ",
        "-" if x & 0x10 else "NMI",
        "TSC" if x & 0x20 else "-",
        ))
    Mode(0)
    OutLowDataByte(x)
    Mode(4)
    Mode(0)

def Mode(x):
    for i in range(18, 21):
        p = machine.Pin(i, machine.Pin.OUT)
        p.value(1 & x)
        x = x >> 1
        
def GetAddr():
    for i in range(16):
        machine.Pin(i, machine.Pin.IN)
    Mode(1)
    addr = 0
    for i in range(16):
        p = machine.Pin(15-i, machine.Pin.IN)
        addr = (addr << 1) | (1 & p.value())
    Mode(0)
    print("addr %04x" % addr)
    return addr

def GetData():
    for i in range(16):
        machine.Pin(i, machine.Pin.IN)
    Mode(2)
    data = 0
    for i in range(16):
        p = machine.Pin(15-i, machine.Pin.IN)
        data = (data << 1) | (1 & p.value())
    Mode(0)
    print("<%04x> data <%02x> %s %s %s %s %s %s" % (
        data,
        255 & data,
        "R" if data & 0x100 else "W",
        "avma" if data & 0x200 else "-",
        "lic" if data & 0x400 else "-",
        "ba" if data & 0x800 else "-",
        "bs" if data & 0x1000 else "-",
        "BUSY" if data & 0x2000 else "-",
        ))
    return data

Latch(0x1C) # reset and halt
   
for k in range(16):
    Qbar.value(0)
    Ebar.value(0)
    Qbar.value(1)
    Ebar.value(1)

Latch(0x1D) # halt

for k in range(16):
    Qbar.value(0)
    Ebar.value(0)
    Qbar.value(1)
    Ebar.value(1)

Latch(0x1F) # run

start = 0
vma = 0
for k in range(2000000):
    Qbar.value(0)
    Ebar.value(0)
    if vma: print("valid:", end=" ")
    if start: print("start:", end=" ")
    addr = GetAddr()
    Qbar.value(1)
    data = GetData()
    reading = (data & 0x100)

    if 0xFF00 <= addr < 0xFFF0:
        print("*** IO PAGE ***")
        break

    # if vma and reading and addr == 0xFFFE: send = 0x80
    # elif vma and reading and addr == 0xFFFF: send = 0x24
    # elif vma and reading: send = 0xBF  # STX extended
    if vma:
        if reading:
            x = Ram[addr]
            Mode(3)
            OutLowDataByte(x)
            print("@ %04x read %02x" % (addr, x))
        else:
            x = (255 & data)
            Ram[addr] = x
            print("@ %04x write %02x" % (addr, x))


    vma = data & 0x200   # AVMA bit means next cycle is Valid
    start = data & 0x400 # LIC bit means next cycle is Start
    Ebar.value(1)
    Mode(0)
    print(';')

#while True:
#    pass
