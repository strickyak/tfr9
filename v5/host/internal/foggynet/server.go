// Package foggynet implements a simulated ESP32 FujiNet + DriveWire server inside tether.
// It simulates WiFi association (SSID "simulated", no password, connected) while making
// live HTTP/HTTPS, TCP, and TNFS internet connections on behalf of guest 6309/NitrOS-9 programs.
package foggynet

import (
	"encoding/binary"
	"fmt"
	"io"
	"net/http"
	"path/filepath"
	"sync"
	"time"
)

const (
	opNOP        = 0x00
	opTime       = 0x23 // '#'
	opInit       = 0x49 // 'I'
	opTerm       = 0x54 // 'T'
	opDWInit     = 0x5A // 'Z'
	opReadEx     = 0xD2
	opReReadEx   = 0xF2
	opWrite      = 0x57 // 'W'
	opReWrite    = 0x77 // 'w'
	opGetStat    = 0x47 // 'G'
	opSetStat    = 0x53 // 'S'
	opSerInit    = 0x45 // 'E'
	opSerTerm    = 0xC5
	opSerGetStat = 0x44 // 'D'
	opSerSetStat = 0xC4
	opSerRead    = 0x43 // 'C'
	opSerReadM   = 0x63 // 'c'
	opSerWrite   = 0xC3
	opSerWriteM  = 0x64 // 'd'
	opPrint      = 0x50 // 'P'
	opPrintFlush = 0x46 // 'F'
	opJeff       = 0xA5
	opFuji       = 0xE2
	opNet        = 0xE3
	opClock      = 0xE5
	opReset1     = 0xFF
	opReset2     = 0xFE
	opReset3     = 0xF8
)

// Options configures a foggynet Server instance.
type Options struct {
	StateDir string
	Logger   func(format string, args ...any)
	HTTP     *http.Client
}

// Server processes a DriveWire + FujiNet byte stream (from Becker port $FF45-$FF46).
type Server struct {
	r          io.Reader
	w          io.Writer
	writeMu    sync.Mutex
	stateDir   string
	logger     func(format string, args ...any)
	httpClient *http.Client

	cfg         persistedConfig
	hostMounted [maxHostSlots]bool
	hostPrefix  [maxHostSlots]string
	tnfsClients [maxHostSlots]*TNFSClient
	disks       [maxDeviceSlots]mountedDisk

	dirOpen  bool
	dirItems []DirItem
	dirPos   int

	akOpen    bool
	akCreator uint16
	akApp     byte
	akKey     byte
	akMode    byte

	fujiLastErr  byte
	fujiLastResp []byte

	netUnits map[byte]*netUnit
}

// NewServer creates and initializes a foggynet Server over rx (6309 -> foggynet) and tx (foggynet -> 6309).
func NewServer(rx io.Reader, tx io.Writer, opts Options) *Server {
	hc := opts.HTTP
	if hc == nil {
		hc = &http.Client{
			Timeout: 20 * time.Second,
		}
	}
	s := &Server{
		r:           rx,
		w:           tx,
		stateDir:    opts.StateDir,
		logger:      opts.Logger,
		httpClient:  hc,
		fujiLastErr: ndevStatusSuccess,
		netUnits:    make(map[byte]*netUnit),
	}
	s.loadOrInitConfig()
	return s
}

func (s *Server) configPath() string {
	return filepath.Join(s.stateDir, "config.json")
}

func (s *Server) sdDir() string {
	return filepath.Join(s.stateDir, "sd")
}

func (s *Server) appKeyDir() string {
	return filepath.Join(s.stateDir, "appkeys")
}

func (s *Server) appKeyFile(creator uint16, app byte, key byte) string {
	return filepath.Join(s.appKeyDir(), fmt.Sprintf("appkey-%04X-%02X-%02X", creator, app, key))
}

func (s *Server) logf(format string, args ...any) {
	if s.logger != nil {
		s.logger(format, args...)
	}
}

func (s *Server) getNetUnit(id byte) *netUnit {
	if id == 0 {
		id = 1
	}
	u, ok := s.netUnits[id]
	if !ok {
		u = newNetUnit(id)
		s.netUnits[id] = u
	}
	return u
}

func (s *Server) readExact(n int) ([]byte, error) {
	buf := make([]byte, n)
	_, err := io.ReadFull(s.r, buf)
	return buf, err
}

func (s *Server) writeBytes(b []byte) error {
	if len(b) == 0 {
		return nil
	}
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	_, err := s.w.Write(b)
	return err
}

// Close closes any open disks, TNFS sessions, and network sockets.
func (s *Server) Close() {
	for i := range s.disks {
		if s.disks[i] != nil {
			s.disks[i].Close()
			s.disks[i] = nil
		}
	}
	for i := range s.tnfsClients {
		if s.tnfsClients[i] != nil {
			_ = s.tnfsClients[i].Close()
			s.tnfsClients[i] = nil
		}
	}
	for _, u := range s.netUnits {
		u.closeConn()
	}
}

func drivewireChecksum(sec []byte) uint16 {
	var sum uint16
	for _, b := range sec {
		sum += uint16(b)
	}
	return sum
}

// Serve runs the DriveWire / FujiNet command loop until rx returns EOF or an error.
func (s *Server) Serve() error {
	defer s.Close()

	var opBuf [1]byte
	for {
		if _, err := io.ReadFull(s.r, opBuf[:]); err != nil {
			if err == io.EOF {
				return nil
			}
			return err
		}
		op := opBuf[0]

		// DriveWire FASTWRITE channels 0..15 (0x80..0x8F + 1 byte)
		if op >= 0x80 && op <= 0x8F {
			if _, err := s.readExact(1); err != nil {
				return err
			}
			continue
		}

		switch op {
		case opNOP, opInit, opTerm, opPrintFlush, opReset1, opReset2, opReset3:
			// No-op commands with zero trailing bytes.

		case opDWInit: // 'Z' + 1B client version -> 0x04
			if _, err := s.readExact(1); err != nil {
				return err
			}
			if err := s.writeBytes([]byte{0x04}); err != nil {
				return err
			}

		case opTime: // '#' -> 6B [yr-1900, mon, day, hr, min, sec]
			now := time.Now()
			reply := [6]byte{
				byte(now.Year() - 1900),
				byte(now.Month()),
				byte(now.Day()),
				byte(now.Hour()),
				byte(now.Minute()),
				byte(now.Second()),
			}
			if err := s.writeBytes(reply[:]); err != nil {
				return err
			}

		case opJeff: // 0xA5 -> "FUJINET"
			if err := s.writeBytes([]byte("FUJINET")); err != nil {
				return err
			}

		case opSerInit, opSerTerm, opPrint:
			if _, err := s.readExact(1); err != nil {
				return err
			}

		case opGetStat, opSetStat, opSerGetStat, opSerWrite:
			if _, err := s.readExact(2); err != nil {
				return err
			}

		case opSerSetStat:
			hdr, err := s.readExact(2)
			if err != nil {
				return err
			}
			if hdr[1] == 0x28 { // SS.ComSt carries 26 option bytes
				if _, err := s.readExact(26); err != nil {
					return err
				}
			}

		case opSerRead: // 'C' -> 2 zero bytes (no virtual serial data pending)
			if err := s.writeBytes([]byte{0x00, 0x00}); err != nil {
				return err
			}

		case opSerReadM:
			if _, err := s.readExact(2); err != nil {
				return err
			}

		case opSerWriteM:
			hdr, err := s.readExact(3)
			if err != nil {
				return err
			}
			if hdr[2] > 0 {
				if _, err := s.readExact(int(hdr[2])); err != nil {
					return err
				}
			}

		case opReadEx, opReReadEx:
			if err := s.handleReadEx(); err != nil {
				return err
			}

		case opWrite, opReWrite:
			if err := s.handleWrite(); err != nil {
				return err
			}

		case opFuji: // 0xE2 + cmd:1 + ...
			cmdBuf, err := s.readExact(1)
			if err != nil {
				return err
			}
			if err := s.handleFujiPacket(cmdBuf[0]); err != nil {
				return err
			}

		case opNet: // 0xE3 + unit:1 + cmd:1 + aux1:1 + aux2:1 + ...
			hdr, err := s.readExact(4)
			if err != nil {
				return err
			}
			if err := s.handleNetPacket(hdr[0], hdr[1], hdr[2], hdr[3]); err != nil {
				return err
			}

		case opClock: // 0xE5 + cmd:1 (APETIME / Simple Clock)
			cmdBuf, err := s.readExact(1)
			if err != nil {
				return err
			}
			if err := s.handleClockPacket(cmdBuf[0]); err != nil {
				return err
			}

		default:
			s.logf("DW unhandled opcode 0x%02X", op)
		}
	}
}

func (s *Server) handleReadEx() error {
	hdr, err := s.readExact(4)
	if err != nil {
		return err
	}
	drv := int(hdr[0])
	lsn := (uint32(hdr[1]) << 16) | (uint32(hdr[2]) << 8) | uint32(hdr[3])

	var sec [256]byte
	rc := byte(0x00)
	if drv < 0 || drv >= maxDeviceSlots || s.disks[drv] == nil {
		rc = 0xF6 // Not ready (still sends 256 zero bytes + reads 2B checksum per drivewire.cpp!)
	} else {
		sec, rc = s.disks[drv].ReadSector(lsn)
		if rc != 0x00 {
			sec = [256]byte{}
		}
	}

	if err := s.writeBytes(sec[:]); err != nil {
		return err
	}
	csumBytes, err := s.readExact(2)
	if err != nil {
		return err
	}
	if rc == 0x00 {
		gotSum := binary.BigEndian.Uint16(csumBytes)
		wantSum := drivewireChecksum(sec[:])
		if gotSum != wantSum {
			rc = 243 // Checksum error
		}
	}
	return s.writeBytes([]byte{rc})
}

func (s *Server) handleWrite() error {
	hdr, err := s.readExact(4)
	if err != nil {
		return err
	}
	drv := int(hdr[0])
	lsn := (uint32(hdr[1]) << 16) | (uint32(hdr[2]) << 8) | uint32(hdr[3])

	secSlice, err := s.readExact(256)
	if err != nil {
		return err
	}
	csumBytes, err := s.readExact(2)
	if err != nil {
		return err
	}
	_ = csumBytes

	if drv < 0 || drv >= maxDeviceSlots || s.disks[drv] == nil {
		return s.writeBytes([]byte{0xF6})
	}
	var sec [256]byte
	copy(sec[:], secSlice)
	rc := s.disks[drv].WriteSector(lsn, sec)
	return s.writeBytes([]byte{rc})
}

func (s *Server) handleClockPacket(cmd byte) error {
	now := time.Now()
	switch cmd {
	case 0x00: // Ready
		return s.writeBytes([]byte{0x01})
	case 0x02: // Error
		return s.writeBytes([]byte{ndevStatusSuccess})
	default: // Return 7-byte simple binary clock [century, yr%100, mon, day, hr, min, sec]
		y := now.Year()
		reply := [7]byte{
			byte(y / 100),
			byte(y % 100),
			byte(now.Month()),
			byte(now.Day()),
			byte(now.Hour()),
			byte(now.Minute()),
			byte(now.Second()),
		}
		return s.writeBytes(reply[:])
	}
}
