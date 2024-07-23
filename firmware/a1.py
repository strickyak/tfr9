import machine

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
    addr = 0
    for i in range(16):
        p = machine.Pin(15-i, machine.Pin.IN)
        addr = (addr << 1) | (1 & p.value())
    Mode(0)
    print("data <%02x> %s %s %s %s %s %s" % (
        255 & addr,
	"R" if addr & 0x100 else "W",
	"avma" if addr & 0x200 else "-",
	"lic" if addr & 0x400 else "-",
	"ba" if addr & 0x800 else "-",
	"bs" if addr & 0x1000 else "-",
	"BUSY" if addr & 0x2000 else "-",
	))
    return addr

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
for k in range(16):
    Qbar.value(0)
    Ebar.value(0)
    if vma: print("valid:", end=" ")
    if start: print("start:", end=" ")
    addr = GetAddr()
    Qbar.value(1)
    d = GetData()
    reading = (d & 0x100)

    send = 0
    if vma and reading and addr == 0xFFFE: send = 0x80
    elif vma and reading and addr == 0xFFFF: send = 0x24
    elif vma and reading: send = 0xBF  # STX extended

    if send:
	print("send %02x" % send)
        Mode(3)
        OutLowDataByte(send)

    vma = d & 0x200
    start = d & 0x400
    Ebar.value(1)
    Mode(0)
    print()

while True:
    pass
