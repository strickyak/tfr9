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

