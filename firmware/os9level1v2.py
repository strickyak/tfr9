import machine
import micropython
import rp2

micropython.alloc_emergency_exception_buf(200)

OUT_HIGH, OUT_LOW, IN_HIGH = rp2.PIO.OUT_HIGH, rp2.PIO.OUT_LOW, rp2.PIO.IN_HIGH

@rp2.asm_pio(
    in_shiftdir=rp2.PIO.SHIFT_LEFT,   # For bits into shift register
    autopush=True,                    # Automatically push the next word from input shift reg into FIFO
    push_thresh=16,                    # Push to the FIFO when 16 bits of the ISR have been produced
    out_shiftdir=rp2.PIO.SHIFT_RIGHT,
    sideset_init=[OUT_HIGH] * 5,
)
def pio_cycle():
    EBAR  = 0b_000_01
    QBAR  = 0b_000_10
    MODE1 = 0b_001_00
    MODE2 = 0b_010_00
    MODE3 = 0b_011_00
    MODE4 = 0b_100_00

    wrap_target()
    label("top")

    set(y, 31)           .side(MODE1 | EBAR | QBAR)
    label("delay1")
    jmp(y_dec, "delay1")

    # Receive Address.
    in_(pins, 16)
    # push()

    set(y, 31)           .side(MODE2 | EBAR) # Raise Q and use mode 2
    label("delay2")
    jmp(y_dec, "delay2")

    # Receive Extra signals (with too-early data bus).
    in_(pins, 16)
    # push()

    # Jump if reading
    jmp(8, "cpu_reads")  # GPIO 8 is R/W, when in MODE2.

    ########################################################
    # CPU WRITING (pico Rx)

    set(y, 31)           .side(MODE2) # Raise E and stay in mode 2
    label("delay3")
    jmp(y_dec, "delay3")

    set(y, 31)           .side(MODE2 | QBAR) # Drop Q and stay in mode 2
    label("delay4")
    jmp(y_dec, "delay4")

    # finally read the data bus.
    in_(pins, 16)
    jmp("top")

    ########################################################
    # CPU READING  ( pico Tx )

    label("cpu_reads")
    set(y, 31)
    pull()               .side(MODE3) # Raise E and use mode 3
    out(pindirs, 16)
    pull()
    out(pins, 16)
    pull()
    out(pindirs, 16)

    set(y, 31)           .side(MODE3) # Raise E and use mode 3
    label("delay5")
    jmp(y_dec, "delay5")

    set(y, 31)           .side(MODE3 | QBAR) # Drop Q and use mode 3
    label("delay6")
    jmp(y_dec, "delay6")

    set(y, 15)           .side(MODE3 | QBAR | QBAR) # Drop E and use mode 3
    label("delay7")          # shorter delay, for cpu to latch Data bus.
    jmp(y_dec, "delay7")

    wrap()
    ########################################################


CYCLE_PIO = 0
CYCLE_SM = 0

class CycleMachine:
    def __init__(self):
        self.sm_cycle = None

    def __enter__(self):
        self.set_up_state_machines()
        return self

    def __exit__(self, *args):
        self.cleanup()

    def set_up_state_machines(self):
        # Remove all existing PIO programs from PIO 0
        rp2.PIO(CYCLE_PIO).remove_program()

        self.sm_cycle = rp2.StateMachine(
            (CYCLE_PIO << 3) + CYCLE_SM, # which state machine in pio0
            pio_cycle,
            freq=125_000_000,
            # freq=125_000_000 >> 4,
            in_base=0,
            out_base=0,
            sideset_base=16,
        )
        print('done set up')

    def start_state_machines(self):
        # self.sm_cycle.restart()
        self.sm_cycle.active(True)
        print('done started')

    def cleanup(self):
        if self.sm_cycle:
            self.sm_cycle.active(False)
            del self.sm_cycle
        rp2.PIO(CYCLE_PIO).remove_program()
        print('done PIO freed')

    pass

def ResetTheCPU():
    def Mode(x):
        for i in range(18, 21):
            p = machine.Pin(i, machine.Pin.OUT)
            p.value(1 & x)
            x = x >> 1
            
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

    Latch(0x1C) # reset and halt

    for k in range(32):
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

ResetTheCPU()

with CycleMachine() as cm:
    cm.start_state_machines()

    start = 0
    vma = 0
    while True:
        print("loop", end=" ")
        while not cm.sm_cycle.rx_fifo():
            pass  # Wait for an address.
        addr = cm.sm_cycle.get()
        print("addr %x" % addr, end=" ")

        while not cm.sm_cycle.rx_fifo():
            pass  # Wait for a flags value.
        flags = cm.sm_cycle.get()
        reading = (flags & 0x100)
        print("flags %x" % addr, end=" ")

        if 0xFF00 <= addr < 0xFFF0:
            print("*** IO PAGE *** addr=%x flags=%x reading=%x" % (addr, flags, reading))
            break

        if vma:
            if reading:
                x = Ram[addr]
                cm.sm_cycle.put(0x0000)
                cm.sm_cycle.put(x)
                cm.sm_cycle.put(0xFFFF)
                if start:
                    print("@ %04x start %02x" % (addr, x))
                else:
                    print("@ %04x read %02x" % (addr, x))
            else:
                print("writing", end=" ")
                while not cm.sm_cycle.rx_fifo():
                    pass  # Wait for a flags value.
                x = 255 & cm.sm_cycle.get()
                Ram[addr] = x
                print("@ %04x write %02x" % (addr, x))
            print(';')
        else:
            cm.sm_cycle.put(0x0000)
            cm.sm_cycle.put(0xA5)
            cm.sm_cycle.put(0xFFFF)
            print('/')

        vma = flags & 0x200   # AVMA bit means next cycle is Valid
        start = flags & 0x400 # LIC bit means next cycle is Start

print("*** Finished.")
