package main

/*

// FIRST IMPLEMENTATION

func Peek1(addr uint) byte {
	phys := Physical(addr)
	return trackRam[phys&RAM_MASK]
}
func Peek2(addr uint) uint {
	hi := Peek1(addr)
	lo := Peek1(addr + 1)
	return (uint(hi) << 8) | uint(lo)
}

*/
