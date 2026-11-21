package main

// PicoRPC: Tether sends requests to the Pico firmware and receives responses.
// This is the reverse direction of VFS RPC.
//
// Uses the same pcb encoding as VFS RPC but with tag T_PICO_RPC (181).

import (
	"github.com/strickyak/tfr9/v5/host/internal/cobs"

	"bytes"
	"encoding/binary"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"io"
	"log"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"
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

// PicoRpcCallReq sends an RpcRequest to the firmware and blocks until
// a response is received or the timeout expires.
func PicoRpcCallReq(channelToPico chan []byte, req RpcRequest, timeout time.Duration) (RpcResponse, error) {
	picoRpcMu.Lock()
	picoRpcSerial++
	serial := picoRpcSerial
	ch := make(chan RpcResponse, 1)
	picoRpcPending[serial] = ch
	picoRpcMu.Unlock()

	req.Serial = serial
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
		return RpcResponse{Status: -1}, fmt.Errorf("PicoRPC timeout: method=%s serial=%d", req.Method, serial)
	}
}

// PicoRpcCall sends a PicoRPC request to the firmware and blocks until
// a response is received or the timeout expires.
func PicoRpcCall(channelToPico chan []byte, method string, data []byte, timeout time.Duration) (RpcResponse, error) {
	return PicoRpcCallReq(channelToPico, RpcRequest{
		Method: method,
		Data:   data,
	}, timeout)
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

// justConnect opens the USB serial port with retries, starts writer and
// reader goroutines, waits for the connection to stabilize, and returns
// the channelToPico for sending packets and a disconnect function.
func justConnect(label string) (chan []byte, func()) {
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

	// Wait briefly for the connection to stabilize.
	time.Sleep(300 * time.Millisecond)

	disconnect := func() {
		// With the poller-managed descriptor (open_linux.go) Close() interrupts the blocked Read and returns
		// at once, really closing the port.  On other platforms it may take long, so bound it (spec S54).
		done := make(chan struct{})
		go func() { serialPort.Close(); close(done) }()
		select {
		case <-done:
		case <-time.After(5 * time.Second):
			log.Printf("%s: closing the serial port timed out; continuing", label)
		}
	}

	return channelToPico, disconnect
}

// waitForDeviceReplaced waits until the serial device at path is no longer the device described by orig: it
// vanished and came back, or it is a different device node now.  After a restart request the board resets
// within 100 ms, but the host can take 10 s or more to notice and re-enumerate; reconnecting before that
// attaches to the dying old device and the new session hears nothing.  Returns false if nothing changed
// within limit (the board did not restart).  If the path cannot be examined at all (Windows COM ports),
// it just sleeps 3 s and returns true.
func waitForDeviceReplaced(path string, orig os.FileInfo, limit time.Duration) bool {
	if orig == nil {
		time.Sleep(3 * time.Second)
		return true
	}
	deadline := time.Now().Add(limit)
	sawGone := false
	for time.Now().Before(deadline) {
		st, err := os.Stat(path)
		if err != nil {
			sawGone = true
		} else if !os.SameFile(orig, st) || sawGone {
			time.Sleep(300 * time.Millisecond) // let the new device settle
			return true
		}
		time.Sleep(200 * time.Millisecond)
	}
	return false
}

// RunJustPing opens the USB serial with retries, sends a single PicoRPC
// ping, prints the result, and exits.
func RunJustPing(value uint32) {
	ch, _ := justConnect("just-ping")
	err := PicoRpcPing(ch, value)
	if err != nil {
		fmt.Fprintf(os.Stderr, "just-ping: FAIL: %v\n", err)
		os.Exit(1)
	}
	time.Sleep(1 * time.Second)
	fmt.Printf("just-ping: OK (value=%d)\n", value)
	os.Exit(0)
}

// RunJustAction opens the USB serial with retries, sends a PicoRPC
// request with the given method (reflash, reformat), prints
// the result, and exits.
func RunJustAction(method string) {
	label := "just-" + method
	ch, disconnect := justConnect(label)
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

// RunJustRestart opens the USB serial with retries, sends a PicoRPC
// restart request with the boot_mode payload, prints the result, and optionally exits.
func RunJustRestart(bootMode uint32, shouldExit bool) {
	label := "just-restart"
	ch, disconnect := justConnect(label)

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
	orig, _ := os.Stat(*WIRE)
	disconnect()
	// The board resets 100 ms after its reply.  Often the host re-enumerates it (the device node is replaced);
	// sometimes the reset is so quick the host never notices.  Either way the port must be closed (done above,
	// and Close() is now interruptible) and reopened, which raises DTR for the new firmware.  Wait for the
	// node to be replaced, but not forever, and never less than 2 s.
	time.Sleep(500 * time.Millisecond)
	if !waitForDeviceReplaced(*WIRE, orig, 8*time.Second) {
		log.Printf("%s: the device node did not change within 8 s (fast reset?); reopening anyway", label)
	}
	if strings.HasPrefix(*WIRE, "/dev/serial/by-id/") {
		// A by-id name follows the board through re-enumeration: keep it, never guess another device.
	} else if matches, _ := filepath.Glob("/dev/ttyACM*"); len(matches) > 0 {
		// Legacy guess ("the last /dev/ttyACM*") -- wrong when more than one board is attached.
		*WIRE = matches[len(matches)-1]
	}
	fmt.Printf("%s: OK\n", label)
	if shouldExit {
		os.Exit(0)
	}
}

// findBootselMounts locates mounted RP2040/RP2350 BOOTSEL volumes (verified by INFO_UF2.TXT)
// across Linux (/media, /run/media) and macOS (/Volumes) without invoking a shell (spec S40).
func findBootselMounts() []string {
	var patterns []string
	if user := os.Getenv("USER"); user != "" {
		patterns = append(patterns,
			filepath.Join("/media", user, "RP*"),
			filepath.Join("/run/media", user, "RP*"),
		)
	}
	patterns = append(patterns,
		"/media/*/RP*",
		"/run/media/*/RP*",
		"/Volumes/RP*",
	)

	seen := make(map[string]bool)
	var mounts []string
	for _, pat := range patterns {
		matches, err := filepath.Glob(pat)
		if err != nil {
			continue
		}
		for _, m := range matches {
			clean := filepath.Clean(m)
			if seen[clean] {
				continue
			}
			st, err := os.Stat(clean)
			if err != nil || !st.IsDir() {
				continue
			}
			if _, err := os.Stat(filepath.Join(clean, "INFO_UF2.TXT")); err != nil {
				continue
			}
			seen[clean] = true
			mounts = append(mounts, clean)
		}
	}
	return mounts
}

// copyUf2ToMount copies uf2path into mountDir directly in Go without spawning a shell.
func copyUf2ToMount(uf2path, mountDir string) (string, error) {
	srcInfo, err := os.Stat(uf2path)
	if err != nil {
		return "", err
	}
	if srcInfo.IsDir() || srcInfo.Size() == 0 {
		return "", fmt.Errorf("invalid UF2 file %q", uf2path)
	}
	src, err := os.Open(uf2path)
	if err != nil {
		return "", err
	}
	defer src.Close()

	dstPath := filepath.Join(mountDir, filepath.Base(uf2path))
	dst, err := os.Create(dstPath)
	if err != nil {
		return dstPath, err
	}
	written, copyErr := io.Copy(dst, src)
	syncErr := dst.Sync()
	closeErr := dst.Close()
	if copyErr != nil {
		return dstPath, copyErr
	}
	if written != srcInfo.Size() {
		return dstPath, fmt.Errorf("short write (%d of %d bytes)", written, srcInfo.Size())
	}
	// When the final UF2 block reaches BOOTSEL, the RP2350 may reboot and detach USB mass storage
	// while Sync/Close finishes; treat Sync/Close errors as fatal only if BOOTSEL is still mounted.
	if syncErr != nil || closeErr != nil {
		if _, stErr := os.Stat(filepath.Join(mountDir, "INFO_UF2.TXT")); stErr == nil {
			if syncErr != nil {
				return dstPath, syncErr
			}
			return dstPath, closeErr
		}
	}
	return dstPath, nil
}

// RunJustReflash sends a reflash RPC to enter BOOTSEL mode, waits for
// the Pico to appear as a USB mass storage device, copies the UF2 file
// to it directly (no /bin/sh), and waits for reboot/unmount.
func RunJustReflash(uf2path string) {
	if st, err := os.Stat(uf2path); err != nil || st.IsDir() || st.Size() == 0 {
		fmt.Fprintf(os.Stderr, "just-reflash: invalid UF2 path %q: %v\n", uf2path, err)
		os.Exit(1)
	}

	ch, disconnect := justConnect("just-reflash")
	resp, err := PicoRpcCall(ch, "reflash", nil, 5*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "just-reflash: FAIL: %v\n", err)
		os.Exit(1)
	}
	if resp.Status != 0 {
		fmt.Fprintf(os.Stderr, "just-reflash: FAIL: status=%d %s\n", resp.Status, resp.Message)
		os.Exit(1)
	}

	// Close the serial port so the host can cleanly re-enumerate when the Pico reboots.
	disconnect()

	fmt.Printf("just-reflash: Pico entering BOOTSEL mode, waiting for mount...\n")
	time.Sleep(1 * time.Second)
	var mounts []string
	deadline := time.Now().Add(12 * time.Second)
	for time.Now().Before(deadline) {
		mounts = findBootselMounts()
		if len(mounts) > 0 {
			break
		}
		time.Sleep(250 * time.Millisecond)
	}
	if len(mounts) == 0 {
		fmt.Fprintf(os.Stderr, "just-reflash: FAIL: no mounted BOOTSEL volume (INFO_UF2.TXT) found within 13s\n")
		os.Exit(1)
	}
	if len(mounts) > 1 {
		fmt.Fprintf(os.Stderr, "just-reflash: FAIL: multiple BOOTSEL volumes mounted (%v); refusing ambiguous flash\n", mounts)
		os.Exit(1)
	}

	dstPath, err := copyUf2ToMount(uf2path, mounts[0])
	fmt.Printf("just-reflash: %s -> %s\n", uf2path, dstPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "just-reflash: copy FAIL: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("just-reflash: copy done, waiting for reboot...\n")
	unmountDeadline := time.Now().Add(8 * time.Second)
	for time.Now().Before(unmountDeadline) {
		if _, err := os.Stat(filepath.Join(mounts[0], "INFO_UF2.TXT")); err != nil {
			break
		}
		time.Sleep(250 * time.Millisecond)
	}
	time.Sleep(2 * time.Second)
	fmt.Printf("just-reflash: OK\n")
	os.Exit(0)
}

// RunJustGetRam fetches the raw ram[] array from the Pico in 256-byte
// chunks via PicoRPC and writes it to filename.
func RunJustGetRam(filename string) {
	label := "just-get-ram"
	ch, disconnect := justConnect(label)
	defer disconnect()

	const chunkSize = 256
	totalRam := 64 * 1024
	var ramBuf []byte

	offset := 0
	for {
		req := RpcRequest{
			Method: "get-ram",
			Offset: offset,
			Length: chunkSize,
		}
		resp, err := PicoRpcCallReq(ch, req, 5*time.Second)
		if err != nil {
			fmt.Fprintf(os.Stderr, "%s: FAIL at offset 0x%04X: %v\n", label, offset, err)
			os.Exit(1)
		}
		if resp.Status != 0 {
			fmt.Fprintf(os.Stderr, "%s: FAIL at offset 0x%04X: status=%d %s\n", label, offset, resp.Status, resp.Message)
			os.Exit(1)
		}
		if offset == 0 {
			if resp.Size > 0 {
				totalRam = resp.Size
			}
			ramBuf = make([]byte, totalRam)
			fmt.Printf("%s: reading %d bytes in %d-byte chunks...\n", label, totalRam, chunkSize)
		}
		if len(resp.Data) == 0 {
			break
		}
		if offset+len(resp.Data) > len(ramBuf) {
			newBuf := make([]byte, offset+len(resp.Data))
			copy(newBuf, ramBuf)
			ramBuf = newBuf
		}
		copy(ramBuf[offset:], resp.Data)
		offset += len(resp.Data)
		if offset >= totalRam {
			break
		}
	}

	err := os.WriteFile(filename, ramBuf, 0666)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: FAIL writing %s: %v\n", label, filename, err)
		os.Exit(1)
	}

	fmt.Printf("%s: OK (saved %d bytes to %s)\n", label, len(ramBuf), filename)
	os.Exit(0)
}

func parseNum(s string) (int, bool) {
	s = strings.TrimSpace(s)
	if s == "" {
		return 0, false
	}
	if strings.HasPrefix(s, "$") {
		v, err := strconv.ParseInt(s[1:], 16, 64)
		if err == nil {
			return int(v), true
		}
		return 0, false
	}
	v, err := strconv.ParseInt(s, 0, 64)
	if err == nil {
		return int(v), true
	}
	return 0, false
}

// parseJustGetTextArg parses "[addr[,width[,height]]]" format.
// If addr is an illegal numeric value like "z", it defaults to 0x0400, 32, 16.
// Omitted trailing numbers default to 32 and 16.
func parseJustGetTextArg(arg string) (int, int, int) {
	addr := 0x0400
	width := 32
	height := 16

	parts := strings.Split(arg, ",")
	if len(parts) > 0 && strings.TrimSpace(parts[0]) != "" {
		if v, ok := parseNum(parts[0]); ok {
			if v >= 0 {
				addr = v
			}
		} else {
			// Illegal numeric values like "z" are an abbreviation for 0x0400, 32, 16.
			return 0x0400, 32, 16
		}
	}
	if len(parts) > 1 && strings.TrimSpace(parts[1]) != "" {
		if v, ok := parseNum(parts[1]); ok && v > 0 {
			width = v
		}
	}
	if len(parts) > 2 && strings.TrimSpace(parts[2]) != "" {
		if v, ok := parseNum(parts[2]); ok && v > 0 {
			height = v
		}
	}
	return addr, width, height
}

// decodeVdgByte converts a VDG byte into standard ASCII.
// If bit 7 ($80) is set (Semigraphics):
//   - If the low 4 bits are $00, replace with a space ' '.
//   - Otherwise, replace with '#'.
// If bit 7 ($80) is clear (Alphanumeric):
//   - Convert 6-bit VDG code to standard ASCII.
func decodeVdgByte(b byte) byte {
	if (b & 0x80) != 0 {
		if (b & 0x0F) == 0 {
			return ' '
		}
		return '#'
	}
	c := b & 0x3F
	if c < 0x20 {
		return c + 0x40
	}
	return c
}

// fetchRamRange fetches length bytes of RAM starting at offset in chunkSize chunks.
func fetchRamRange(ch chan []byte, offset, length int) ([]byte, error) {
	const chunkSize = 256
	var buf []byte
	curOffset := offset
	remaining := length

	for remaining > 0 {
		reqLen := chunkSize
		if reqLen > remaining {
			reqLen = remaining
		}
		req := RpcRequest{
			Method: "get-ram",
			Offset: curOffset,
			Length: reqLen,
		}
		resp, err := PicoRpcCallReq(ch, req, 5*time.Second)
		if err != nil {
			return nil, fmt.Errorf("FAIL at offset 0x%04X: %v", curOffset, err)
		}
		if resp.Status != 0 {
			return nil, fmt.Errorf("FAIL at offset 0x%04X: status=%d %s", curOffset, resp.Status, resp.Message)
		}
		if len(resp.Data) == 0 {
			break
		}
		buf = append(buf, resp.Data...)
		curOffset += len(resp.Data)
		remaining -= len(resp.Data)
	}
	return buf, nil
}

// WriteRamRange writes a slice of bytes into Pico RAM via put-ram RPC in 256-byte chunks.
func WriteRamRange(ch chan []byte, offset int, data []byte) error {
	const chunkSize = 256
	curOffset := offset
	remaining := len(data)
	dataPos := 0

	for remaining > 0 {
		reqLen := chunkSize
		if reqLen > remaining {
			reqLen = remaining
		}
		req := RpcRequest{
			Method: "put-ram",
			Offset: curOffset,
			Data:   data[dataPos : dataPos+reqLen],
		}
		resp, err := PicoRpcCallReq(ch, req, 5*time.Second)
		if err != nil {
			return fmt.Errorf("FAIL at offset 0x%04X: %v", curOffset, err)
		}
		if resp.Status != 0 {
			return fmt.Errorf("FAIL at offset 0x%04X: status=%d %s", curOffset, resp.Status, resp.Message)
		}
		curOffset += reqLen
		dataPos += reqLen
		remaining -= reqLen
	}
	return nil
}

// RunJustGetText fetches the text screen from Pico RAM via get-ram,
// converts the 6-bit VDG values to normal ASCII, and prints lines to stdout.
func RunJustGetText(arg string) {
	label := "just-get-text"
	addr, width, height := parseJustGetTextArg(arg)

	ch, disconnect := justConnect(label)
	defer disconnect()

	data, err := fetchRamRange(ch, addr, width*height)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: %v\n", label, err)
		os.Exit(1)
	}

	for row := 0; row < height; row++ {
		start := row * width
		end := start + width
		var line strings.Builder
		for i := start; i < end; i++ {
			if i < len(data) {
				line.WriteByte(decodeVdgByte(data[i]))
			} else {
				line.WriteByte(' ')
			}
		}
		fmt.Println(line.String())
	}
	os.Exit(0)
}

// CpsToTicks converts characters per second to 20ms tick counts for key down and key up.
// Default rate is 5.0 cps.
func CpsToTicks(cps float64) (downTicks, upTicks int) {
	if cps <= 0 {
		cps = 5.0
	}
	// 1 second has 50 ticks of 20ms.
	totalTicks := int(math.Round(50.0 / cps))
	if totalTicks < 2 {
		totalTicks = 2
	}
	if totalTicks > 60000 {
		totalTicks = 60000
	}
	downTicks = totalTicks / 2
	upTicks = totalTicks - downTicks
	return downTicks, upTicks
}

// parseJustGetPmodeArg parses the argument to --just-get-pmode=[M,P,C,filename].
// M: PMODE mode number (0 to 4, default 4).
// P: Start page number (1 for "PMODE 4,1"). If P > 15, P is treated directly as the RAM address.
//    If P <= 15 (and P >= 1), address is 0x0600 + (P-1)*0x0600. Default P = 1 (0x0600).
// C: Colorset (0 or 1, e.g. "SCREEN 1,C"). Default C = 1.
// filename: Output PNG filename. Default "pmode<M>.png".
func parseJustGetPmodeArg(arg string) (int, int, int, string) {
	mode := 4
	pageOrAddr := 1
	colorset := 1
	filename := ""

	parts := strings.Split(arg, ",")
	if len(parts) > 0 && strings.TrimSpace(parts[0]) != "" {
		if v, ok := parseNum(parts[0]); ok && v >= 0 && v <= 4 {
			mode = v
		}
	}
	if len(parts) > 1 && strings.TrimSpace(parts[1]) != "" {
		if v, ok := parseNum(parts[1]); ok && v >= 0 {
			pageOrAddr = v
		}
	}
	if len(parts) > 2 && strings.TrimSpace(parts[2]) != "" {
		if v, ok := parseNum(parts[2]); ok {
			colorset = v
		}
	}
	if len(parts) > 3 && strings.TrimSpace(parts[3]) != "" {
		filename = strings.TrimSpace(parts[3])
	}
	if filename == "" {
		filename = fmt.Sprintf("pmode%d.png", mode)
	}

	var addr int
	if pageOrAddr > 15 {
		addr = pageOrAddr
	} else if pageOrAddr >= 1 {
		addr = 0x0600 + (pageOrAddr-1)*0x0600
	} else {
		addr = 0x0600
	}
	return mode, addr, colorset, filename
}

// RunJustGetPmode fetches the PMODE framebuffer from Pico RAM via get-ram,
// renders it as a PNG image with the specified mode and colorset,
// and saves it to a PNG file.
func RunJustGetPmode(arg string) {
	label := "just-get-pmode"
	mode, addr, colorset, filename := parseJustGetPmodeArg(arg)

	ch, disconnect := justConnect(label)
	defer disconnect()

	var width, height, bpp int
	switch mode {
	case 0:
		width, height, bpp = 128, 96, 1
	case 1:
		width, height, bpp = 128, 96, 2
	case 2:
		width, height, bpp = 128, 192, 1
	case 3:
		width, height, bpp = 128, 192, 2
	case 4:
		width, height, bpp = 256, 192, 1
	default:
		width, height, bpp = 256, 192, 1
	}

	bytesPerRow := width * bpp / 8
	totalBytes := bytesPerRow * height

	data, err := fetchRamRange(ch, addr, totalBytes)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: %v\n", label, err)
		os.Exit(1)
	}

	// In CoCo 3 MMU mode, Slot 0 ($0000-$1FFF) maps to Block 8 ($10000-$11FFF).
	// If the buffer fetched from addr is all zeros, check $10000 + addr (or addr - $10000).
	isAllZero := true
	for _, b := range data {
		if b != 0 {
			isAllZero = false
			break
		}
	}
	if isAllZero {
		altAddr := addr
		if addr < 0x10000 {
			altAddr = addr + 0x10000
		} else {
			altAddr = addr - 0x10000
		}
		altData, err := fetchRamRange(ch, altAddr, totalBytes)
		if err == nil {
			for _, b := range altData {
				if b != 0 {
					data = altData
					addr = altAddr
					break
				}
			}
		}
	}

	img := image.NewRGBA(image.Rect(0, 0, width, height))

	var palette []color.RGBA
	if bpp == 1 {
		// 2-color modes: Background is black.
		// Foreground is Green (colorset 0) or Buff/White (colorset != 0).
		bg := color.RGBA{R: 0, G: 0, B: 0, A: 255}
		fg := color.RGBA{R: 200, G: 200, B: 200, A: 255}
		if colorset == 0 {
			fg = color.RGBA{R: 50, G: 200, B: 50, A: 255}
		}
		palette = []color.RGBA{bg, fg}

		for y := 0; y < height; y++ {
			rowOffset := y * bytesPerRow
			for byteCol := 0; byteCol < bytesPerRow; byteCol++ {
				var b byte
				if rowOffset+byteCol < len(data) {
					b = data[rowOffset+byteCol]
				}
				for bit := 0; bit < 8; bit++ {
					x := byteCol*8 + bit
					if (b & (1 << (7 - bit))) != 0 {
						img.Set(x, y, palette[1])
					} else {
						img.Set(x, y, palette[0])
					}
				}
			}
		}
	} else {
		// 4-color modes:
		// colorset 0: Green, Yellow, Blue, Red
		// colorset != 0: Buff/White, Cyan, Magenta, Orange
		if colorset == 0 {
			palette = []color.RGBA{
				{R: 50, G: 200, B: 50, A: 255},  // Green
				{R: 220, G: 220, B: 50, A: 255}, // Yellow
				{R: 50, G: 80, B: 220, A: 255},  // Blue
				{R: 200, G: 50, B: 50, A: 255},  // Red
			}
		} else {
			palette = []color.RGBA{
				{R: 220, G: 220, B: 220, A: 255}, // Buff/White
				{R: 50, G: 200, B: 200, A: 255},  // Cyan
				{R: 200, G: 50, B: 200, A: 255},  // Magenta
				{R: 220, G: 140, B: 40, A: 255},  // Orange
			}
		}

		for y := 0; y < height; y++ {
			rowOffset := y * bytesPerRow
			for byteCol := 0; byteCol < bytesPerRow; byteCol++ {
				var b byte
				if rowOffset+byteCol < len(data) {
					b = data[rowOffset+byteCol]
				}
				for p := 0; p < 4; p++ {
					x := byteCol*4 + p
					colIdx := (b >> (6 - p*2)) & 0x03
					img.Set(x, y, palette[colIdx])
				}
			}
		}
	}

	outFile, err := os.Create(filename)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: cannot create %s: %v\n", label, filename, err)
		os.Exit(1)
	}
	defer outFile.Close()

	if err := png.Encode(outFile, img); err != nil {
		fmt.Fprintf(os.Stderr, "%s: cannot encode png: %v\n", label, err)
		os.Exit(1)
	}

	fmt.Printf("just-get-pmode: OK (saved %dx%d PMODE %d PNG to %s from 0x%04X, colorset=%d)\n",
		width, height, mode, filename, addr, colorset)
	os.Exit(0)
}

func RunJustGetPmode4(arg string) {
	RunJustGetPmode(arg)
}

// parseJustGetHscreenArg parses the argument to --just-get-hscreen=[mode,]filename.png.
// mode: HSCREEN mode number (1 to 4, default 2).
// filename: Output PNG filename (default "hscreen<mode>.png").
func parseJustGetHscreenArg(arg string) (int, string) {
	mode := 2
	filename := "hscreen2.png"

	parts := strings.Split(arg, ",")
	if len(parts) == 1 {
		s := strings.TrimSpace(parts[0])
		if v, ok := parseNum(s); ok && v >= 1 && v <= 4 {
			mode = v
			filename = fmt.Sprintf("hscreen%d.png", mode)
		} else if s != "" {
			filename = s
		}
	} else if len(parts) >= 2 {
		if v, ok := parseNum(parts[0]); ok && v >= 1 && v <= 4 {
			mode = v
		}
		if strings.TrimSpace(parts[1]) != "" {
			filename = strings.TrimSpace(parts[1])
		}
	}
	return mode, filename
}

// coco3ColorToRGBA converts a 6-bit CoCo 3 palette code (0..63) to color.RGBA.
func coco3ColorToRGBA(code byte) color.RGBA {
	switch code {
	case 0:
		return color.RGBA{R: 0, G: 0, B: 0, A: 255} // Black
	case 63:
		return color.RGBA{R: 240, G: 240, B: 240, A: 255} // White
	case 18:
		return color.RGBA{R: 50, G: 200, B: 50, A: 255} // Green
	case 36:
		return color.RGBA{R: 220, G: 220, B: 50, A: 255} // Yellow
	case 11:
		return color.RGBA{R: 50, G: 80, B: 220, A: 255} // Blue
	case 7:
		return color.RGBA{R: 220, G: 50, B: 50, A: 255} // Red
	case 31:
		return color.RGBA{R: 50, G: 200, B: 200, A: 255} // Cyan
	case 9:
		return color.RGBA{R: 200, G: 50, B: 200, A: 255} // Magenta
	case 38:
		return color.RGBA{R: 240, G: 140, B: 40, A: 255} // Orange
	case 54:
		return color.RGBA{R: 220, G: 220, B: 50, A: 255} // RGB Yellow
	case 27:
		return color.RGBA{R: 50, G: 200, B: 200, A: 255} // RGB Cyan
	case 45:
		return color.RGBA{R: 200, G: 50, B: 200, A: 255} // RGB Magenta
	default:
		// CoCo 3 GIME 6-bit RGB layout: bit5=R1, bit4=G1, bit3=B1, bit2=R0, bit1=G0, bit0=B0
		r := uint8((((code >> 4) & 2) | ((code >> 2) & 1)) * 85)
		g := uint8((((code >> 3) & 2) | ((code >> 1) & 1)) * 85)
		b := uint8((((code >> 2) & 2) | (code & 1)) * 85)
		return color.RGBA{R: r, G: g, B: b, A: 255}
	}
}

// RunJustGetHscreen fetches the CoCo 3 HSCREEN framebuffer from Pico RAM via get-ram,
// renders it as a PNG image with the specified HSCREEN mode and palette,
// and saves it to a PNG file.
func RunJustGetHscreen(arg string) {
	label := "just-get-hscreen"
	mode, filename := parseJustGetHscreenArg(arg)

	ch, disconnect := justConnect(label)
	defer disconnect()

	var width, height, bpp int
	switch mode {
	case 1:
		width, height, bpp = 320, 192, 2
	case 2:
		width, height, bpp = 320, 192, 4
	case 3:
		width, height, bpp = 640, 192, 1
	case 4:
		width, height, bpp = 640, 192, 2
	default:
		width, height, bpp = 320, 192, 4
	}

	bytesPerRow := width * bpp / 8
	totalBytes := bytesPerRow * height
	addr := 0x00000 // In CoCo 3, Task 1 graphics buffer is at physical Block 0..3 (0x00000)

	data, err := fetchRamRange(ch, addr, totalBytes)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: %v\n", label, err)
		os.Exit(1)
	}

	// Default 16-color composite palette
	defaultPalette := []color.RGBA{
		{R: 50, G: 200, B: 50, A: 255},   // 0: Green
		{R: 220, G: 220, B: 50, A: 255},  // 1: Yellow
		{R: 50, G: 80, B: 220, A: 255},   // 2: Blue
		{R: 220, G: 50, B: 50, A: 255},   // 3: Red
		{R: 240, G: 240, B: 240, A: 255}, // 4: White
		{R: 50, G: 200, B: 200, A: 255},  // 5: Cyan
		{R: 200, G: 50, B: 200, A: 255},  // 6: Magenta
		{R: 240, G: 140, B: 40, A: 255},  // 7: Orange
		{R: 0, G: 0, B: 0, A: 255},       // 8: Black
		{R: 50, G: 200, B: 50, A: 255},   // 9: Green
		{R: 0, G: 0, B: 0, A: 255},       // 10: Black
		{R: 240, G: 240, B: 240, A: 255}, // 11: White
		{R: 0, G: 0, B: 0, A: 255},       // 12: Black
		{R: 50, G: 200, B: 50, A: 255},   // 13: Green
		{R: 0, G: 0, B: 0, A: 255},       // 14: Black
		{R: 240, G: 140, B: 40, A: 255},  // 15: Orange
	}

	palette := make([]color.RGBA, 16)
	copy(palette, defaultPalette)

	// Fetch palette registers $FFB0-$FFBF from Pico RAM
	palData, err := fetchRamRange(ch, 0xFFB0, 16)
	if err == nil && len(palData) == 16 {
		for i := 0; i < 16; i++ {
			code := palData[i] & 0x3F
			palette[i] = coco3ColorToRGBA(code)
		}
	}

	img := image.NewRGBA(image.Rect(0, 0, width, height))

	for y := 0; y < height; y++ {
		rowOffset := y * bytesPerRow
		for byteCol := 0; byteCol < bytesPerRow; byteCol++ {
			var b byte
			if rowOffset+byteCol < len(data) {
				b = data[rowOffset+byteCol]
			}
			switch bpp {
			case 4: // 2 pixels per byte (16 colors)
				p0 := (b >> 4) & 0x0F
				p1 := b & 0x0F
				img.Set(byteCol*2, y, palette[p0])
				img.Set(byteCol*2+1, y, palette[p1])
			case 2: // 4 pixels per byte (4 colors)
				for p := 0; p < 4; p++ {
					colIdx := (b >> (6 - p*2)) & 0x03
					img.Set(byteCol*4+p, y, palette[colIdx])
				}
			case 1: // 8 pixels per byte (2 colors)
				for bit := 0; bit < 8; bit++ {
					colIdx := 0
					if (b & (1 << (7 - bit))) != 0 {
						colIdx = 1
					}
					img.Set(byteCol*8+bit, y, palette[colIdx])
				}
			}
		}
	}

	outFile, err := os.Create(filename)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: cannot create %s: %v\n", label, filename, err)
		os.Exit(1)
	}
	defer outFile.Close()

	if err := png.Encode(outFile, img); err != nil {
		fmt.Fprintf(os.Stderr, "%s: cannot encode png: %v\n", label, err)
		os.Exit(1)
	}

	fmt.Printf("just-get-hscreen: OK (saved %dx%d HSCREEN %d PNG to %s)\n",
		width, height, mode, filename)
	os.Exit(0)
}

// RunJustGetCycles fetches real-time MCU status and the last 256 bus cycles.
func RunJustGetCycles() {
	label := "just-get-cycles"
	ch, disconnect := justConnect(label)
	defer disconnect()

	// 1. Get Status
	reqStatus := RpcRequest{Method: "get-status"}
	respStatus, err := PicoRpcCallReq(ch, reqStatus, 3*time.Second)
	if err == nil && respStatus.Status == 0 {
		fmt.Printf("STATUS: %s\n", respStatus.Message)
	} else if err != nil {
		fmt.Fprintf(os.Stderr, "get-status err: %v\n", err)
	}

	// 2. Get Cycles
	reqCycles := RpcRequest{Method: "get-cycles"}
	respCycles, err := PicoRpcCallReq(ch, reqCycles, 3*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: FAIL: %v\n", label, err)
		os.Exit(1)
	}
	data := respCycles.Data
	count := len(data) / 4
	fmt.Printf("LAST %d BUS CYCLES:\n", count)
	for i := 0; i < count; i++ {
		abus := (uint16(data[i*4]) << 8) | uint16(data[i*4+1])
		dbus := data[i*4+2]
		flags := data[i*4+3]
		rw := "W"
		if (flags & 1) != 0 {
			rw = "R"
		}
		fmt.Printf("  [%3d] %s $%04X -> $%02X\n", i, rw, abus, dbus)
	}
	os.Exit(0)
}

// RunJustGetLabel opens the USB serial with retries, sends a get-label
// PicoRPC request, prints the label string on a line, and exits.
func RunJustGetLabel() {
	label := "just-get-label"
	ch, disconnect := justConnect(label)
	defer disconnect()

	resp, err := PicoRpcCall(ch, "get-label", nil, 5*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: FAIL: %v\n", label, err)
		os.Exit(1)
	}
	if resp.Status != 0 {
		fmt.Fprintf(os.Stderr, "%s: FAIL: status=%d %s\n", label, resp.Status, resp.Message)
		os.Exit(1)
	}
	fmt.Printf("%s\n", string(resp.Data))
	os.Exit(0)
}

// RunJustSetLabel validates labelData, sends a set-label PicoRPC request
// (accepted in Phase 1 TCL> only), verifies the echoed label, and exits.
func RunJustSetLabel(labelData string) {
	label := "just-set-label"
	if err := ValidateLabelString(labelData); err != nil {
		log.Fatalf("%v", err)
	}

	ch, disconnect := justConnect(label)
	defer disconnect()

	resp, err := PicoRpcCall(ch, "set-label", []byte(labelData), 5*time.Second)
	if err != nil {
		fmt.Fprintf(os.Stderr, "%s: FAIL: %v\n", label, err)
		os.Exit(1)
	}
	if resp.Status != 0 {
		fmt.Fprintf(os.Stderr, "%s: FAIL: status=%d %s\n", label, resp.Status, resp.Message)
		os.Exit(1)
	}
	if string(resp.Data) != labelData {
		fmt.Fprintf(os.Stderr, "%s: FAIL: readback %q != %q\n", label, string(resp.Data), labelData)
		os.Exit(1)
	}
	fmt.Printf("%s: OK (%s)\n", label, string(resp.Data))
	os.Exit(0)
}



