package main

import ()

type Plain struct { // implements Personality
}

func (o *Plain) HasMMap() bool               { return false }
func (o *Plain) CurrentHardwareMMap() string { return "hwm?" }

func (o *Plain) MemoryModuleOf(addr uint) (name string, offset uint) {
	return "plain?", offset
}
func (o *Plain) Os9String(addr uint) string {
	return "?Os9String?"
}

func (o *Plain) FormatReturn(os9num byte, call *Os9ApiCall, rec *EventRec) (string, *Regs) {
	panic("FormatReturn")
}

func (o *Plain) FormatCall(os9num byte, call *Os9ApiCall, rec *EventRec) (string, *Regs) {
	panic("FormatCall")
}

func (o *Plain) RegisteredMemoryModules() (z []*ScannedModuleInfo) {
	return
}
