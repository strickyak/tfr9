from machine import Pin
import time

def JustBlink():
    while True:
        for i in range(2):
            Led.value(i)
            time.sleep(0.2)

Led = Pin("LED", Pin.OUT)
Led(1)

def DirectionIn():
    Pin(0, Pin.IN, Pin.PULL_UP)
    Pin(1, Pin.IN, Pin.PULL_UP)
    Pin(2, Pin.IN, Pin.PULL_UP)
    Pin(3, Pin.IN, Pin.PULL_UP)
    Pin(4, Pin.IN, Pin.PULL_UP)
    Pin(5, Pin.IN, Pin.PULL_UP)
    Pin(6, Pin.IN, Pin.PULL_UP)
    Pin(7, Pin.IN, Pin.PULL_UP)

def DataIn():
    z =  0x01 if Pin(0, Pin.IN, Pin.PULL_UP).value() else 0
    z |= 0x02 if Pin(1, Pin.IN, Pin.PULL_UP).value() else 0
    z |= 0x04 if Pin(2, Pin.IN, Pin.PULL_UP).value() else 0
    z |= 0x08 if Pin(3, Pin.IN, Pin.PULL_UP).value() else 0
    z |= 0x10 if Pin(4, Pin.IN, Pin.PULL_UP).value() else 0
    z |= 0x20 if Pin(5, Pin.IN, Pin.PULL_UP).value() else 0
    z |= 0x40 if Pin(6, Pin.IN, Pin.PULL_UP).value() else 0
    z |= 0x80 if Pin(7, Pin.IN, Pin.PULL_UP).value() else 0
    return z

def DataOut(x):
    Pin(0, Pin.OUT).value(1 if (x & 1) else 0)
    Pin(1, Pin.OUT).value(1 if (x & 2) else 0)
    Pin(2, Pin.OUT).value(1 if (x & 4) else 0)
    Pin(3, Pin.OUT).value(1 if (x & 8) else 0)
    Pin(4, Pin.OUT).value(1 if (x & 0x10) else 0)
    Pin(5, Pin.OUT).value(1 if (x & 0x20) else 0)
    Pin(6, Pin.OUT).value(1 if (x & 0x40) else 0)
    Pin(7, Pin.OUT).value(1 if (x & 0x80) else 0)

def DownUp(pin):
    pin.value(0)
    pin.value(1)
    

DirectionIn()

E = Pin(8, Pin.OUT)
E.value(0)
Q = Pin(9, Pin.OUT)
Q.value(0)
 
CounterClock = Pin(10, Pin.OUT)
CounterClock.value(1)
CounterReset = Pin(11, Pin.OUT)
CounterReset.value(1)

def Y(n):
    CounterReset.value(0)
    CounterReset.value(1)
    for i in range(n):
        CounterClock.value(0)
        CounterClock.value(1)

def Latch(x, SignalY6=False):
    Y(5)
    DataOut(x)
    if SignalY6:
        CounterClock.value(0)
        CounterClock.value(1)
    Y(0)
    DirectionIn()

# Latch vaulues:
NOTHING = 0xFF
RESET = 0xFE
RESET_HALT = 0xFC
HALT = 0xFD
IRQ = 0xFB
FIRQ = 0xF7
NMI = 0xEF
Latch(RESET)

def NullCycles(n):
    for i in range(n):
        Q.value(1)
        E.value(1)
        Q.value(0)
        E.value(0)

def TryLatches():
    Latch(RESET, SignalY6=True)
    NullCycles(8)
    Latch(RESET_HALT)
    NullCycles(2)
    Latch(HALT)
    NullCycles(2)
    Latch(IRQ)
    NullCycles(2)
    Latch(FIRQ)
    NullCycles(2)
    Latch(NMI)
    NullCycles(2)
    Latch(NOTHING)
    NullCycles(4)

def Reset():
    Latch(RESET, SignalY6=True)
    NullCycles(20)
    Latch(NOTHING)
    NullCycles(1)

def NormalCycle():
    DirectionIn()
    E.value(0)           # phase 1
    DownUp(CounterReset) # -> Y0
    alo = DataIn()
    Q.value(1)           # phase 2
    DownUp(CounterClock) # -> Y1
    ahi = DataIn()
    E.value(1)           # phase 3
    DownUp(CounterClock) # -> Y2
    status = DataIn()
    Q.value(0)           # phase 4
    DownUp(CounterClock) # -> Y3

    if status & 1:       # CASE: 6809 READ CYCLE
        DownUp(CounterClock) # -> Y4
        DataOut(ReadRam(ahi, alo))
        E.value(0)           # phase 1

    else:                # CASE: 6809 WRITE CYCLE
        data = DataIn()
        WriteRam(ahi, alo, data)
        
def ReadRam(ahi, alo):
    z = 0x0C  # INC Direct
    print ("r %02x %02x -> %02x" % (ahi, alo, z))
    return z

def WriteRam(ahi, alo, data):
    print ("w %02x %02x <- %02x w" % (ahi, alo, data))
    pass

if True:
    Reset()
    for i in range(100):
        NormalCycle()
pass
