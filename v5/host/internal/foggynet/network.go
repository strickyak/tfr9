package foggynet

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"
)

const (
	ndevStatusSuccess      = 0x01
	ndevStatusWriteOnly    = 0x83
	ndevStatusInvalidSpec  = 0xA5
	ndevStatusEOF          = 0x88 // 136
	ndevStatusNotConnected = 0x80 // 128
	ndevStatusGeneralErr   = 0x90 // 144

	parserNone = 0
	parserJSON = 1

	httpChanBody           = 0
	httpChanCollectHeaders = 1
	httpChanGetHeaders     = 2
	httpChanSetHeaders     = 3
	httpChanPostSetData    = 4
)

type netUnit struct {
	id         byte
	prefix     string
	login      string
	password   string
	openMode   byte
	transMode  byte
	parserMode byte
	lineEnding string

	// Active transport kind: "", "http", "tcp"
	kind string

	// HTTP state
	httpURL      string
	httpMethod   string
	httpChanMode byte
	httpHeaders  map[string]string
	httpPostBody []byte
	httpFetched  bool
	httpBody     []byte
	httpPos      int
	httpStatus   int

	// JSON parser state
	jsonParsed bool
	jsonDoc    any
	jsonBuf    []byte
	jsonPos    int

	// TCP state
	tcpMu     sync.Mutex
	tcpConn   net.Conn
	tcpBuf    []byte
	tcpClosed bool
	tcpErr    error

	// Last transaction result for OP_NET 0x02 / 0x01
	lastErr  byte
	lastResp []byte
}

func newNetUnit(id byte) *netUnit {
	return &netUnit{
		id:          id,
		lineEnding:  "\n",
		httpHeaders: make(map[string]string),
		lastErr:     ndevStatusSuccess,
	}
}

func (u *netUnit) closeConn() {
	u.tcpMu.Lock()
	if u.tcpConn != nil {
		_ = u.tcpConn.Close()
		u.tcpConn = nil
	}
	u.tcpBuf = nil
	u.tcpClosed = true
	u.tcpMu.Unlock()

	u.kind = ""
	u.httpURL = ""
	u.httpMethod = ""
	u.httpChanMode = httpChanBody
	u.httpHeaders = make(map[string]string)
	u.httpPostBody = nil
	u.httpFetched = false
	u.httpBody = nil
	u.httpPos = 0
	u.parserMode = parserNone
	u.jsonParsed = false
	u.jsonDoc = nil
	u.jsonBuf = nil
	u.jsonPos = 0
}

func trimDeviceSpec(raw []byte) string {
	// Stop at NUL or CR or LF.
	for i, b := range raw {
		if b == 0 || b == '\r' || b == '\n' {
			raw = raw[:i]
			break
		}
	}
	return strings.TrimSpace(string(raw))
}

func normalizeDeviceSpec(spec string, prefix string) (scheme string, cleanURL string, err error) {
	s := strings.TrimSpace(spec)
	// Strip leading N:, N1:, n2:, etc.
	if len(s) >= 2 && (s[0] == 'N' || s[0] == 'n') {
		if s[1] == ':' {
			s = s[2:]
		} else if len(s) >= 3 && s[1] >= '0' && s[1] <= '9' && s[2] == ':' {
			s = s[3:]
		}
	}
	if !strings.Contains(s, "://") && prefix != "" {
		s = strings.TrimRight(prefix, "/") + "/" + strings.TrimLeft(s, "/")
	}
	idx := strings.Index(s, "://")
	if idx <= 0 {
		return "", "", fmt.Errorf("missing scheme in %q", spec)
	}
	scheme = strings.ToLower(s[:idx])
	rest := s[idx+3:]

	switch scheme {
	case "http", "https":
		// Lowercase scheme and host (for TLS SNI when guest passes HTTPS://FUJINET.ONLINE/...),
		// while preserving path and query case!
		slashIdx := strings.IndexAny(rest, "/?")
		var hostPart, pathPart string
		if slashIdx >= 0 {
			hostPart = strings.ToLower(rest[:slashIdx])
			pathPart = rest[slashIdx:]
		} else {
			hostPart = strings.ToLower(rest)
			pathPart = "/"
		}
		full := scheme + "://" + hostPart + pathPart
		if _, parseErr := url.Parse(full); parseErr != nil {
			return "", "", parseErr
		}
		return scheme, full, nil

	case "tcp":
		hostPort := strings.TrimRight(rest, "/")
		if !strings.Contains(hostPort, ":") {
			return "", "", fmt.Errorf("tcp spec missing :port in %q", spec)
		}
		return "tcp", hostPort, nil

	default:
		return "", "", fmt.Errorf("unsupported scheme %q", scheme)
	}
}

func applyEOLTranslation(data []byte, trans byte) []byte {
	switch trans {
	case 0: // NONE
		return data
	case 1: // CR (CoCo / OS-9)
		out := bytes.ReplaceAll(data, []byte("\r\n"), []byte("\r"))
		return bytes.ReplaceAll(out, []byte("\n"), []byte("\r"))
	case 2: // LF
		return bytes.ReplaceAll(data, []byte("\r\n"), []byte("\n"))
	case 3: // CRLF
		norm := bytes.ReplaceAll(data, []byte("\r\n"), []byte("\n"))
		norm = bytes.ReplaceAll(norm, []byte("\r"), []byte("\n"))
		return bytes.ReplaceAll(norm, []byte("\n"), []byte("\r\n"))
	default:
		return data
	}
}

func (s *Server) ensureHTTPFetched(u *netUnit) error {
	if u.httpFetched {
		return nil
	}
	method := u.httpMethod
	if method == "" {
		method = "GET"
	}
	var bodyReader io.Reader
	if len(u.httpPostBody) > 0 {
		bodyReader = bytes.NewReader(u.httpPostBody)
	}
	req, err := http.NewRequest(method, u.httpURL, bodyReader)
	if err != nil {
		return err
	}
	req.Header.Set("User-Agent", "FujiNet/foggynet-1.0")
	if method == "POST" && len(u.httpHeaders) == 0 {
		req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	}
	for k, v := range u.httpHeaders {
		req.Header.Set(k, v)
	}
	if u.login != "" || u.password != "" {
		req.SetBasicAuth(u.login, u.password)
	}

	s.logf("HTTP %s %s", method, u.httpURL)
	resp, err := s.httpClient.Do(req)
	if err != nil {
		s.logf("HTTP error on %s: %v", u.httpURL, err)
		return err
	}
	defer resp.Body.Close()

	raw, err := io.ReadAll(io.LimitReader(resp.Body, 4*1024*1024))
	if err != nil {
		return err
	}
	u.httpStatus = resp.StatusCode
	u.httpBody = applyEOLTranslation(raw, u.transMode)
	u.httpPos = 0
	u.httpFetched = true
	s.logf("HTTP %s -> %d (%d bytes)", u.httpURL, resp.StatusCode, len(u.httpBody))
	return nil
}

func (s *Server) startTCPReader(u *netUnit, conn net.Conn) {
	go func() {
		buf := make([]byte, 2048)
		for {
			n, err := conn.Read(buf)
			if n > 0 {
				u.tcpMu.Lock()
				u.tcpBuf = append(u.tcpBuf, buf[:n]...)
				u.tcpMu.Unlock()
			}
			if err != nil {
				u.tcpMu.Lock()
				u.tcpClosed = true
				u.tcpErr = err
				u.tcpMu.Unlock()
				return
			}
		}
	}()
}

func (s *Server) handleNetPacket(unitID byte, cmd byte, aux1 byte, aux2 byte) error {
	u := s.getNetUnit(unitID)
	param16 := (uint16(aux1) << 8) | uint16(aux2)

	switch cmd {
	case 0x00: // FUJI_DEVICE_READY
		return s.writeBytes([]byte{0x01})

	case 0x02: // FUJI_SEND_ERROR
		return s.writeBytes([]byte{u.lastErr})

	case 0x01: // FUJI_SEND_RESPONSE
		reqLen := int(param16)
		out := u.lastResp
		if reqLen > len(out) {
			padded := make([]byte, reqLen)
			copy(padded, out)
			out = padded
		} else if reqLen > 0 && reqLen < len(out) {
			out = out[:reqLen]
		}
		return s.writeBytes(out)

	case 'O': // NET_OPEN (0x4F) + 256-byte devicespec
		rawSpec, err := s.readExact(256)
		if err != nil {
			return err
		}
		u.closeConn()
		u.openMode = aux1
		u.transMode = aux2
		spec := trimDeviceSpec(rawSpec)
		scheme, target, normErr := normalizeDeviceSpec(spec, u.prefix)
		if normErr != nil {
			s.logf("NET[%d] OPEN invalid spec %q: %v", unitID, spec, normErr)
			u.lastErr = ndevStatusInvalidSpec
			return nil
		}

		switch scheme {
		case "http", "https":
			u.kind = "http"
			u.httpURL = target
			switch aux1 {
			case 0x0D:
				u.httpMethod = "POST"
			case 0x08, 0x0E:
				u.httpMethod = "PUT"
			case 0x05, 0x09:
				u.httpMethod = "DELETE"
			default:
				u.httpMethod = "GET"
			}
			u.lastErr = ndevStatusSuccess
			s.logf("NET[%d] OPEN %s %s (mode=0x%02X trans=0x%02X)", unitID, u.httpMethod, u.httpURL, aux1, aux2)

		case "tcp":
			s.logf("NET[%d] OPEN TCP %s", unitID, target)
			conn, dialErr := net.DialTimeout("tcp", target, 10*time.Second)
			if dialErr != nil {
				s.logf("NET[%d] TCP dial error %s: %v", unitID, target, dialErr)
				u.lastErr = ndevStatusGeneralErr
				return nil
			}
			u.kind = "tcp"
			u.tcpMu.Lock()
			u.tcpConn = conn
			u.tcpBuf = nil
			u.tcpClosed = false
			u.tcpErr = nil
			u.tcpMu.Unlock()
			s.startTCPReader(u, conn)
			u.lastErr = ndevStatusSuccess
		}
		return nil

	case 'C': // NET_CLOSE (0x43)
		s.logf("NET[%d] CLOSE", unitID)
		u.closeConn()
		u.lastErr = ndevStatusSuccess
		u.lastResp = nil
		return nil

	case 0xFC: // NET_SET_PARSER
		// DriveWire sends parser mode in aux1 (drivewireNetwork.cpp), standard NDevice in aux2.
		mode := aux1
		if mode == 0 && aux2 != 0 {
			mode = aux2
		}
		u.parserMode = mode
		u.jsonParsed = false
		u.jsonDoc = nil
		u.jsonBuf = nil
		u.jsonPos = 0
		u.lastErr = ndevStatusSuccess
		return nil

	case 0xFB: // NET_SET_PARAMETER
		if aux1 == 1 { // PARSER_PARAM::EOL
			u.lineEnding = string([]byte{aux2})
		}
		u.lastErr = ndevStatusSuccess
		return nil

	case 'T': // NET_TRANSLATION (0x54)
		u.transMode = aux1
		u.lastErr = ndevStatusSuccess
		return nil

	case 'M': // NET_SET_HTTP_MODE (0x4D)
		mode := aux2
		if mode == 0 && aux1 != 0 {
			mode = aux1
		}
		u.httpChanMode = mode
		u.lastErr = ndevStatusSuccess
		return nil

	case 'P': // NET_PARSE (0x50)
		if u.kind != "http" {
			u.lastErr = ndevStatusGeneralErr
			return nil
		}
		if err := s.ensureHTTPFetched(u); err != nil {
			u.lastErr = ndevStatusGeneralErr
			return nil
		}
		doc, err := ParseJSON(u.httpBody)
		if err != nil {
			s.logf("NET[%d] JSON parse error: %v", unitID, err)
			u.lastErr = ndevStatusGeneralErr
			return nil
		}
		u.jsonDoc = doc
		u.jsonParsed = true
		u.jsonBuf = nil
		u.jsonPos = 0
		u.lastErr = ndevStatusSuccess
		return nil

	case 'Q': // NET_QUERY (0x51) + 256-byte query
		rawQ, err := s.readExact(256)
		if err != nil {
			return err
		}
		qStr := trimDeviceSpec(rawQ)
		if !u.jsonParsed {
			u.lastErr = ndevStatusGeneralErr
			return nil
		}
		val := QueryJSON(u.jsonDoc, qStr)
		s.logf("NET[%d] JSON query %q -> %q", unitID, qStr, val)
		if val == "" {
			u.jsonBuf = nil
			u.jsonPos = 0
		} else {
			u.jsonBuf = []byte(val + u.lineEnding)
			u.jsonPos = 0
		}
		u.lastErr = ndevStatusSuccess
		return nil

	case 'S': // NET_STATUS (0x53) -> 4-byte [avail_be16, conn, err]
		var avail uint16
		var conn byte
		var errCode byte = ndevStatusSuccess

		switch u.kind {
		case "http":
			if u.parserMode == parserJSON && u.jsonParsed {
				rem := len(u.jsonBuf) - u.jsonPos
				if rem > 65535 {
					rem = 65535
				}
				avail = uint16(rem)
				if rem > 0 {
					conn = 1
					errCode = ndevStatusSuccess
				} else {
					conn = 0
					errCode = ndevStatusEOF
				}
			} else {
				if err := s.ensureHTTPFetched(u); err != nil {
					avail = 0
					conn = 0
					errCode = ndevStatusGeneralErr
				} else {
					rem := len(u.httpBody) - u.httpPos
					if rem > 65535 {
						rem = 65535
					}
					avail = uint16(rem)
					if rem > 0 {
						conn = 1
						errCode = ndevStatusSuccess
					} else {
						conn = 0
						errCode = ndevStatusEOF
					}
				}
			}

		case "tcp":
			u.tcpMu.Lock()
			rem := len(u.tcpBuf)
			closed := u.tcpClosed
			u.tcpMu.Unlock()
			if rem > 65535 {
				rem = 65535
			}
			avail = uint16(rem)
			if !closed {
				conn = 1
				errCode = ndevStatusSuccess
			} else if rem > 0 {
				conn = 1
				errCode = ndevStatusSuccess
			} else {
				conn = 0
				errCode = ndevStatusEOF
			}

		default:
			avail = 0
			conn = 0
			errCode = ndevStatusNotConnected
		}

		var st [4]byte
		binary.BigEndian.PutUint16(st[0:2], avail)
		st[2] = conn
		st[3] = errCode
		u.lastResp = st[:]
		u.lastErr = ndevStatusSuccess
		return nil

	case 'R': // NET_READ (0x52)
		numBytes := int(param16)
		if numBytes <= 0 {
			u.lastErr = ndevStatusGeneralErr
			u.lastResp = nil
			return nil
		}

		switch u.kind {
		case "http":
			if u.parserMode == parserJSON && u.jsonParsed {
				rem := len(u.jsonBuf) - u.jsonPos
				if rem <= 0 {
					// Return empty padded buffer with EOF (treated as success by fujinet-lib & fnwx).
					u.lastResp = nil
					u.lastErr = ndevStatusEOF
					return nil
				}
				take := numBytes
				if take > rem {
					take = rem
				}
				u.lastResp = append([]byte(nil), u.jsonBuf[u.jsonPos:u.jsonPos+take]...)
				u.jsonPos += take
				u.lastErr = ndevStatusSuccess
				return nil
			}

			if err := s.ensureHTTPFetched(u); err != nil {
				u.lastResp = nil
				u.lastErr = ndevStatusGeneralErr
				return nil
			}
			rem := len(u.httpBody) - u.httpPos
			if rem <= 0 {
				u.lastResp = nil
				u.lastErr = ndevStatusEOF
				return nil
			}
			take := numBytes
			if take > rem {
				take = rem
			}
			u.lastResp = append([]byte(nil), u.httpBody[u.httpPos:u.httpPos+take]...)
			u.httpPos += take
			u.lastErr = ndevStatusSuccess
			return nil

		case "tcp":
			// Wait briefly if buffer is empty and connection is still open (matches network_read).
			deadline := time.Now().Add(2 * time.Second)
			for {
				u.tcpMu.Lock()
				if len(u.tcpBuf) > 0 || u.tcpClosed {
					take := numBytes
					if take > len(u.tcpBuf) {
						take = len(u.tcpBuf)
					}
					if take > 0 {
						u.lastResp = append([]byte(nil), u.tcpBuf[:take]...)
						u.tcpBuf = u.tcpBuf[take:]
						u.lastErr = ndevStatusSuccess
					} else {
						u.lastResp = nil
						u.lastErr = ndevStatusEOF
					}
					u.tcpMu.Unlock()
					return nil
				}
				u.tcpMu.Unlock()
				if time.Now().After(deadline) {
					u.lastResp = nil
					u.lastErr = ndevStatusSuccess
					return nil
				}
				time.Sleep(5 * time.Millisecond)
			}

		default:
			u.lastResp = nil
			u.lastErr = ndevStatusNotConnected
			return nil
		}

	case 'W': // NET_WRITE (0x57) + numBytes payload
		numBytes := int(param16)
		var payload []byte
		if numBytes > 0 {
			var err error
			payload, err = s.readExact(numBytes)
			if err != nil {
				return err
			}
		}

		switch u.kind {
		case "http":
			switch u.httpChanMode {
			case httpChanSetHeaders:
				hdr := strings.TrimRight(string(payload), "\r\n\x00")
				if idx := strings.IndexByte(hdr, ':'); idx > 0 {
					u.httpHeaders[strings.TrimSpace(hdr[:idx])] = strings.TrimSpace(hdr[idx+1:])
				}
			default:
				u.httpPostBody = append(u.httpPostBody, payload...)
				u.httpFetched = false
			}
			u.lastErr = ndevStatusSuccess
			return nil

		case "tcp":
			u.tcpMu.Lock()
			conn := u.tcpConn
			u.tcpMu.Unlock()
			if conn == nil {
				u.lastErr = ndevStatusNotConnected
				return nil
			}
			if len(payload) > 0 {
				if _, err := conn.Write(payload); err != nil {
					u.lastErr = ndevStatusGeneralErr
					return nil
				}
			}
			u.lastErr = ndevStatusSuccess
			return nil

		default:
			u.lastErr = ndevStatusNotConnected
			return nil
		}

	case 0x2C: // NET_CHDIR (',') + 256B prefix
		raw, err := s.readExact(256)
		if err != nil {
			return err
		}
		u.prefix = trimDeviceSpec(raw)
		u.lastErr = ndevStatusSuccess
		return nil

	case 0x30: // NET_GETCWD ('0')
		buf := make([]byte, 256)
		copy(buf, u.prefix)
		u.lastResp = buf
		u.lastErr = ndevStatusSuccess
		return nil

	case 0xFD: // NET_USERNAME + 256B
		raw, err := s.readExact(256)
		if err != nil {
			return err
		}
		u.login = trimDeviceSpec(raw)
		u.lastErr = ndevStatusSuccess
		return nil

	case 0xFE: // NET_PASSWORD + 256B
		raw, err := s.readExact(256)
		if err != nil {
			return err
		}
		u.password = trimDeviceSpec(raw)
		u.lastErr = ndevStatusSuccess
		return nil

	default:
		s.logf("NET[%d] unhandled cmd 0x%02X (aux=%02X %02X)", unitID, cmd, aux1, aux2)
		u.lastErr = ndevStatusGeneralErr
		return nil
	}
}
