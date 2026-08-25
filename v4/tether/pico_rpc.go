package main

// PicoRPC: Tether sends requests to the Pico firmware and receives responses.
// This is the reverse direction of VFS RPC.
//
// Uses the same pcb encoding as VFS RPC but with tag T_PICO_RPC (181).

import (
	"github.com/strickyak/tfr9/v4/tether/cobs"

	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"sync"
	"time"
)

// EncodeRpcRequest encodes an RpcRequest into pcb wire format.
func EncodeRpcRequest(req RpcRequest) []byte {
	var buf bytes.Buffer
	if req.Method != "" {
		putStr(&buf, 1, req.Method)
	}
	if req.Serial != 0 {
		putInt(&buf, 2, int64(req.Serial))
	}
	if req.Path != "" {
		putStr(&buf, 3, req.Path)
	}
	if req.Path2 != "" {
		putStr(&buf, 4, req.Path2)
	}
	if req.Handle != 0 {
		putInt(&buf, 5, int64(req.Handle))
	}
	if req.Flags != 0 {
		putInt(&buf, 6, int64(req.Flags))
	}
	if req.Length != 0 {
		putInt(&buf, 7, int64(req.Length))
	}
	if len(req.Data) > 0 {
		putBytes(&buf, 8, req.Data)
	}
	if req.Offset != 0 {
		putInt(&buf, 9, int64(req.Offset))
	}
	if req.Whence != 0 {
		putInt(&buf, 10, int64(req.Whence))
	}
	buf.WriteByte(0) // Terminator
	return buf.Bytes()
}

// DecodeRpcResponse decodes a pcb-encoded RpcResponse from the firmware.
func DecodeRpcResponse(buf []byte) RpcResponse {
	var resp RpcResponse
	offset := 0
	for offset < len(buf) {
		tag := buf[offset]
		offset++
		if tag == 0 {
			break
		}

		fieldNum := tag >> 3
		kind := tag & 7

		if kind == KIND_INT {
			val := int64(getVarInt(buf, &offset))
			switch fieldNum {
			case 1:
				resp.Status = int(val)
			case 2:
				resp.Serial = int(val)
			case 3:
				resp.Handle = int(val)
			case 5:
				resp.Size = int(val)
			case 6:
				resp.IsDir = int(val)
			}
		} else if kind == KIND_STR {
			length := int(getVarInt(buf, &offset))
			sBuf := buf[offset : offset+length]
			offset += length
			switch fieldNum {
			case 1:
				resp.Message = string(sBuf)
			case 4:
				resp.Data = sBuf
			}
		}
	}
	return resp
}

// PicoRPC infrastructure: pending request tracking

var picoRpcMu sync.Mutex
var picoRpcSerial int
var picoRpcPending = make(map[int]chan RpcResponse)

// HandlePicoRpcResponse is called by the packet dispatcher when a
// T_PICO_RPC response arrives from the firmware.
func HandlePicoRpcResponse(payload []byte) {
	resp := DecodeRpcResponse(payload)
	picoRpcMu.Lock()
	ch, ok := picoRpcPending[resp.Serial]
	if ok {
		delete(picoRpcPending, resp.Serial)
	}
	picoRpcMu.Unlock()
	if ok {
		ch <- resp
	} else {
		log.Printf("PicoRPC: unexpected response serial=%d", resp.Serial)
	}
}

// PicoRpcCall sends a PicoRPC request to the firmware and blocks until
// a response is received or the timeout expires.
func PicoRpcCall(channelToPico chan []byte, method string, data []byte, timeout time.Duration) (RpcResponse, error) {
	picoRpcMu.Lock()
	picoRpcSerial++
	serial := picoRpcSerial
	ch := make(chan RpcResponse, 1)
	picoRpcPending[serial] = ch
	picoRpcMu.Unlock()

	req := RpcRequest{
		Method: method,
		Serial: serial,
		Data:   data,
	}
	encoded := EncodeRpcRequest(req)
	packet := append([]byte{T_PICO_RPC}, encoded...)
	WriteBytes(channelToPico, packet...)

	select {
	case resp := <-ch:
		return resp, nil
	case <-time.After(timeout):
		picoRpcMu.Lock()
		delete(picoRpcPending, serial)
		picoRpcMu.Unlock()
		return RpcResponse{Status: -1}, fmt.Errorf("PicoRPC timeout: method=%s serial=%d", method, serial)
	}
}

// PicoRpcPing sends a ping request with a uint32 payload and verifies
// the firmware echoes it back.
func PicoRpcPing(channelToPico chan []byte, value uint32) error {
	payload := make([]byte, 4)
	binary.LittleEndian.PutUint32(payload, value)
	resp, err := PicoRpcCall(channelToPico, "ping", payload, 5*time.Second)
	if err != nil {
		return err
	}
	if resp.Status != 0 {
		return fmt.Errorf("PicoRPC ping: status=%d message=%s", resp.Status, resp.Message)
	}
	if len(resp.Data) < 4 {
		return fmt.Errorf("PicoRPC ping: response data too short (%d bytes)", len(resp.Data))
	}
	got := binary.LittleEndian.Uint32(resp.Data)
	if got != value {
		return fmt.Errorf("PicoRPC ping: expected %d, got %d", value, got)
	}
	return nil
}

// quickConnect opens the USB serial port with retries, starts writer and
// reader goroutines, waits for the connection to stabilize, and returns
// the channelToPico for sending packets and a disconnect function.
func quickConnect(label string) (chan []byte, func()) {
	serialOptions := OpenSerialOptions{
		PortName:        *WIRE,
		BaudRate:        *BAUD,
		DataBits:        8,
		StopBits:        1,
		MinimumReadSize: 1,
	}

	// Try to open the serial port up to 3 times, 1 second apart.
	var serialPort io.ReadWriteCloser
	var err error
	for attempt := 0; attempt < 3; attempt++ {
		serialPort, err = OpenSerial(serialOptions)
		if err == nil {
			break
		}
		log.Printf("%s: open attempt %d failed: %v", label, attempt+1, err)
		time.Sleep(1 * time.Second)
	}
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: cannot open %s after 3 attempts: %v\n", label, *WIRE, err)
		os.Exit(1)
	}

	// Send a raw 0 to abort any partial COBS packet, then T_HELLO.
	serialPort.Write([]byte{0x00})
	helloEncoded := cobs.Encode([]byte{T_HELLO, 129, 0})
	helloEncoded = append(helloEncoded, 0x00)
	serialPort.Write(helloEncoded)

	// Channel for sending packets to the Pico.
	channelToPico := make(chan []byte, 64)

	// Writer goroutine: sends COBS-encoded packets to the serial port.
	go func() {
		for packet := range channelToPico {
			encoded := cobs.Encode(packet)
			encoded = append(encoded, 0x00)
			serialPort.Write(encoded)
		}
	}()

	// Reader + COBS decoder: reads bytes from serial, decodes COBS packets,
	// and dispatches T_PICO_RPC responses.
	go func() {
		buf := make([]byte, 1024)
		var currentPacket []byte
		for {
			n, err := serialPort.Read(buf)
			if err != nil {
				return
			}
			for i := 0; i < n; i++ {
				b := buf[i]
				if b == 0 {
					if len(currentPacket) > 0 {
						decoded, err := cobs.Decode(currentPacket)
						if err == nil && len(decoded) > 0 {
							if decoded[0] == T_PICO_RPC {
								HandlePicoRpcResponse(decoded[1:])
							}
							// Ignore all other packet types silently.
						}
						currentPacket = nil
					}
				} else {
					currentPacket = append(currentPacket, b)
				}
			}
		}
	}()

	// Wait for the connection to stabilize.
	time.Sleep(1 * time.Second)

	disconnect := func() {
		serialPort.Close()
	}

	return channelToPico, disconnect
}

// RunQuickPing opens the USB serial with retries, sends a single PicoRPC
// ping, prints the result, and exits.
func RunQuickPing(value uint32) {
	ch, _ := quickConnect("quick-ping")
	err := PicoRpcPing(ch, value)
	if err != nil {
		fmt.Fprintf(os.Stderr, "quick-ping: FAIL: %v\n", err)
		os.Exit(1)
	}
	time.Sleep(1 * time.Second)
	fmt.Printf("quick-ping: OK (value=%d)\n", value)
	os.Exit(0)
}

// RunQuickAction opens the USB serial with retries, sends a PicoRPC
// request with the given method (reflash, reformat), prints
// the result, and exits.
func RunQuickAction(method string) {
	label := "quick-" + method
	ch, disconnect := quickConnect(label)
	resp, err := PicoRpcCall(ch, method, nil, 5*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: FAIL: %v\n", label, err)
		os.Exit(1)
	}
	if resp.Status != 0 {
		fmt.Fprintf(os.Stderr, "%s: FAIL: status=%d %s\n", label, resp.Status, resp.Message)
		os.Exit(1)
	}
	// Close the serial port so Linux can cleanly re-enumerate
	// /dev/ttyACM0 if the Pico reboots (e.g. restart).
	disconnect()
	if method == "restart" {
		time.Sleep(3 * time.Second)
	}
	fmt.Printf("%s: OK\n", label)
	os.Exit(0)
}

// RunQuickRestart opens the USB serial with retries, sends a PicoRPC
// restart request with the boot_mode payload, prints the result, and optionally exits.
func RunQuickRestart(bootMode uint32, shouldExit bool) {
	label := "quick-restart"
	ch, disconnect := quickConnect(label)

	payload := make([]byte, 4)
	payload[0] = byte(bootMode >> 24)
	payload[1] = byte(bootMode >> 16)
	payload[2] = byte(bootMode >> 8)
	payload[3] = byte(bootMode)

	resp, err := PicoRpcCall(ch, "restart", payload, 5*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: FAIL: %v\n", label, err)
		if shouldExit { os.Exit(1) } else { return }
	}
	if resp.Status != 0 {
		fmt.Fprintf(os.Stderr, "%s: FAIL: status=%d %s\n", label, resp.Status, resp.Message)
		if shouldExit { os.Exit(1) } else { return }
	}
	// Close the serial port so Linux can cleanly re-enumerate
	// /dev/ttyACM0 if the Pico reboots.
	disconnect()
	time.Sleep(3 * time.Second)
	fmt.Printf("%s: OK\n", label)
	if shouldExit {
		os.Exit(0)
	}
}

// RunQuickReflash sends a reflash RPC to enter BOOTSEL mode, waits for
// the Pico to appear as a USB mass storage device, copies the UF2 file
// to it, and waits for the automatic unmount.
func RunQuickReflash(uf2path string) {
	ch, disconnect := quickConnect("quick-reflash")
	resp, err := PicoRpcCall(ch, "reflash", nil, 5*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "quick-reflash: FAIL: %v\n", err)
		os.Exit(1)
	}
	if resp.Status != 0 {
		fmt.Fprintf(os.Stderr, "quick-reflash: FAIL: status=%d %s\n", resp.Status, resp.Message)
		os.Exit(1)
	}

	// Close the serial port so Linux can cleanly re-enumerate
	// /dev/ttyACM0 when the Pico reboots after flashing.
	disconnect()

	fmt.Printf("quick-reflash: Pico entering BOOTSEL mode, waiting 5s for mount...\n")
	time.Sleep(5 * time.Second)

	// Copy the UF2 file to the mounted RP2350 mass storage device.
	cpCmd := fmt.Sprintf("cp -v '%s' /media/${USER}/RP*", uf2path)
	fmt.Printf("quick-reflash: %s\n", cpCmd)
	cmd := exec.Command("/bin/sh", "-c", cpCmd)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	err = cmd.Run()
	if err != nil {
		fmt.Fprintf(os.Stderr, "quick-reflash: copy FAIL: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("quick-reflash: copy done, waiting 5s for unmount...\n")
	time.Sleep(5 * time.Second)
	fmt.Printf("quick-reflash: OK\n")
	os.Exit(0)
}
