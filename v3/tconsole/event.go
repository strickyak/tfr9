package main

import (
	"bytes"
	"fmt"
)

const ANGLES = false

func OnEvent(fromUSB <-chan byte, pending map[string]*EventRec) {
	event := getByte(fromUSB)
	sz := getByte(fromUSB)

	pc1 := getByte(fromUSB)
	pc2 := getByte(fromUSB)
	op_pc := (uint(pc1) << 8) | uint(pc2)

	cy1 := getByte(fromUSB)
	cy2 := getByte(fromUSB)
	cy3 := getByte(fromUSB)
	cy4 := getByte(fromUSB)
	op_cy := (uint(cy1) << 24) | (uint(cy2) << 16) | (uint(cy3) << 8) | uint(cy4)

	datas := make([]byte, sz)
	addrs := make([]uint, sz)
	for i := byte(0); i < sz; i++ {
		one := getByte(fromUSB)
		two := getByte(fromUSB)
		three := getByte(fromUSB)
		datas[i] = one
		addrs[i] = (uint(two) << 8) | uint(three)
	}
	eventName, ok := CommandStrings[event]
	if !ok {
		Fatalf("Unknown event number: %d.", event)
	}

	{
		var buf bytes.Buffer
		for i := 0; i < int(sz); i++ {
			fmt.Fprintf(&buf, " %04x:%02x", addrs[i], datas[i])
		}
		Logf("C_EVENT %q $%04x #%d:%s", eventName, op_pc, op_cy, buf.String())
	}

	rec := &EventRec{
		Number: event,
		PC:     op_pc,
		Cycle:  op_cy,
		Datas:  datas,
		Addrs:  addrs,
	}

	switch event {
	case EVENT_SWI2:
		if ANGLES {
			fmt.Printf("<")
		}

		os9num := rec.Datas[0]
		rec.Os9Num = os9num
		call, _ := Os9ApiCallOf[os9num]
		rec.SerialNum = MintSerialNum()
		callString, regs := FormatCall(os9num, call, rec)
		rec.Call = callString

		lastPC := op_pc
		key := Format("%04x_%04x", lastPC, addrs[2])
		Logf("\n%q === OS9_CALL _%d_ %q:  %s #%d ...... %s\n\n", Who(), rec.SerialNum, key, rec.Call, op_cy, CurrentMapString())
		pending[key] = rec

		if os9num == 0x8A /* I$Write */ {
			HandleWrite(regs)
		}

		if os9num == 0x8C /* I$WritLn */ {
			HandleWritLn(regs)
		}

	case EVENT_RTI:
		if ANGLES {
			fmt.Printf(">")
		}

		// TODO: 6309
		// TODO: FIRQ
		pc1, pc2 := datas[10], datas[11]
		ret_pc := (uint(pc1) << 8) | uint(pc2)
		ret_sp := addrs[11]

		key := Format("%04x_%04x", ret_pc-3, ret_sp)
		caller, ok := pending[key]

		if ok {
			os9num := caller.Os9Num
			call, _ := Os9ApiCallOf[os9num]
			returnString, regs := FormatReturn(caller.Os9Num, call, rec)
			_ = regs
			Logf("\n%q === OS9_RETURN _%d_ %q:  %s #%d --> %s #%d %s\n\n", Who(), caller.SerialNum, key, caller.Call, caller.Cycle, returnString, op_cy, CurrentMapString())
			delete(pending, key)
		} else {
			Logf("\n%q === OS9_RETURN _%d_ %q:xxxxxxx #%d \n\n", Who(), rec.SerialNum, key, op_cy)
		}

		// DumpRam()
	} // end switch event
}
