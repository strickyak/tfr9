package foggynet

import (
	"encoding/binary"
	"fmt"
	"net"
	"path"
	"sort"
	"strings"
	"sync"
	"time"
)

const (
	tnfsDefaultPort = 16384

	tnfsCmdMount    = 0x00
	tnfsCmdUmount   = 0x01
	tnfsCmdOpenDir  = 0x10
	tnfsCmdReadDir  = 0x11
	tnfsCmdCloseDir = 0x12
	tnfsCmdOpenDirX = 0x17
	tnfsCmdReadDirX = 0x18

	tnfsCmdRead  = 0x21
	tnfsCmdWrite = 0x22
	tnfsCmdClose = 0x23
	tnfsCmdStat  = 0x24
	tnfsCmdLseek = 0x25
	tnfsCmdOpen  = 0x29

	tnfsErrEOF = 0x21
)

// DirItem represents a directory entry on either a local SD folder or a remote TNFS server.
type DirItem struct {
	Name    string
	IsDir   bool
	Size    uint32
	ModTime time.Time
}

// TNFSClient speaks the TNFS UDP protocol (port 16384) to a remote TNFS host.
type TNFSClient struct {
	mu      sync.Mutex
	host    string
	conn    *net.UDPConn
	sessID  [2]byte
	seq     byte
	mounted bool
}

// DialTNFS connects and mounts "/" on a TNFS server (default UDP port 16384).
func DialTNFS(hostSpec string) (*TNFSClient, error) {
	addrStr := strings.TrimSpace(hostSpec)
	addrStr = strings.TrimPrefix(strings.TrimPrefix(addrStr, "tnfs://"), "TNFS://")
	if idx := strings.IndexByte(addrStr, '/'); idx >= 0 {
		addrStr = addrStr[:idx]
	}
	if !strings.Contains(addrStr, ":") {
		addrStr = fmt.Sprintf("%s:%d", addrStr, tnfsDefaultPort)
	}
	raddr, err := net.ResolveUDPAddr("udp", addrStr)
	if err != nil {
		return nil, fmt.Errorf("tnfs resolve %q: %w", addrStr, err)
	}
	conn, err := net.DialUDP("udp", nil, raddr)
	if err != nil {
		return nil, fmt.Errorf("tnfs dial %q: %w", addrStr, err)
	}

	c := &TNFSClient{
		host: hostSpec,
		conn: conn,
	}
	if err := c.mount("/"); err != nil {
		_ = conn.Close()
		return nil, err
	}
	return c, nil
}

func (c *TNFSClient) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.conn == nil {
		return nil
	}
	if c.mounted {
		_, _ = c.rpcLocked(tnfsCmdUmount, nil)
		c.mounted = false
	}
	err := c.conn.Close()
	c.conn = nil
	return err
}

func (c *TNFSClient) mount(mountPoint string) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	// Payload: version 1.2 (LSB=0x02, MSB=0x01), mountPoint\0, user\0, pass\0
	payload := make([]byte, 0, len(mountPoint)+5)
	payload = append(payload, 0x02, 0x01)
	payload = append(payload, []byte(mountPoint)...)
	payload = append(payload, 0x00, 0x00, 0x00)

	c.sessID = [2]byte{0, 0}
	resp, err := c.rpcLocked(tnfsCmdMount, payload)
	if err != nil {
		return err
	}
	if len(resp) < 5 || resp[4] != 0x00 {
		code := byte(0xFF)
		if len(resp) >= 5 {
			code = resp[4]
		}
		return fmt.Errorf("tnfs mount failed (status 0x%02X)", code)
	}
	c.sessID[0] = resp[0]
	c.sessID[1] = resp[1]
	c.mounted = true
	return nil
}

func (c *TNFSClient) rpcLocked(cmd byte, payload []byte) ([]byte, error) {
	if c.conn == nil {
		return nil, fmt.Errorf("tnfs connection closed")
	}
	seq := c.seq
	c.seq++

	pkt := make([]byte, 4+len(payload))
	pkt[0] = c.sessID[0]
	pkt[1] = c.sessID[1]
	pkt[2] = seq
	pkt[3] = cmd
	copy(pkt[4:], payload)

	var respBuf [2048]byte
	for attempt := 0; attempt < 3; attempt++ {
		if _, err := c.conn.Write(pkt); err != nil {
			return nil, err
		}
		deadline := time.Now().Add(2500 * time.Millisecond)
		for {
			_ = c.conn.SetReadDeadline(deadline)
			n, err := c.conn.Read(respBuf[:])
			if err != nil {
				if ne, ok := err.(net.Error); ok && ne.Timeout() {
					break // retry write
				}
				return nil, err
			}
			if n >= 4 && respBuf[2] == seq && respBuf[3] == cmd {
				out := make([]byte, n)
				copy(out, respBuf[:n])
				return out, nil
			}
		}
	}
	return nil, fmt.Errorf("tnfs timeout on cmd 0x%02X to %s", cmd, c.host)
}

func cleanTNFSPath(p string) string {
	if p == "" {
		return "/"
	}
	cleaned := path.Clean("/" + strings.TrimSpace(p))
	if cleaned == "" {
		return "/"
	}
	return cleaned
}

// ListDir returns sorted directory entries (directories first, then files) from the TNFS host.
func (c *TNFSClient) ListDir(dirPath string, pattern string) ([]DirItem, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	cleanDir := cleanTNFSPath(dirPath)
	if items, err := c.listDirXLocked(cleanDir, pattern); err == nil {
		return items, nil
	}
	return c.listDirBasicLocked(cleanDir, pattern)
}

func (c *TNFSClient) listDirXLocked(cleanDir string, pattern string) ([]DirItem, error) {
	// OPENDIRX (0x17): diropt:1, sortopt:1, maxresults_le16:2, wildcard\0, path\0
	var req []byte
	req = append(req, 0x00, 0x00, 0x00, 0x00)
	req = append(req, []byte(pattern)...)
	req = append(req, 0x00)
	req = append(req, []byte(cleanDir)...)
	req = append(req, 0x00)

	resp, err := c.rpcLocked(tnfsCmdOpenDirX, req)
	if err != nil {
		return nil, err
	}
	if len(resp) < 6 || resp[4] != 0x00 {
		return nil, fmt.Errorf("tnfs opendirx unsupported or failed")
	}
	dh := resp[5]
	defer func() {
		_, _ = c.rpcLocked(tnfsCmdCloseDir, []byte{dh})
	}()

	var items []DirItem
	for {
		rresp, err := c.rpcLocked(tnfsCmdReadDirX, []byte{dh, 0})
		if err != nil {
			return nil, err
		}
		if len(rresp) < 5 {
			return nil, fmt.Errorf("tnfs short readdirx")
		}
		if rresp[4] == tnfsErrEOF {
			break
		}
		if rresp[4] != 0x00 {
			return nil, fmt.Errorf("tnfs readdirx error 0x%02X", rresp[4])
		}
		if len(rresp) < 9 {
			break
		}
		count := int(rresp[5])
		dirstat := rresp[6]
		pos := 9 // skip count(1), status(1), dirpos(2)
		for i := 0; i < count && pos+13 < len(rresp); i++ {
			flags := rresp[pos]
			sz := binary.LittleEndian.Uint32(rresp[pos+1 : pos+5])
			mtime := binary.LittleEndian.Uint32(rresp[pos+5 : pos+9])
			pos += 13 // flags(1) + size(4) + mtime(4) + ctime(4)
			nul := pos
			for nul < len(rresp) && rresp[nul] != 0 {
				nul++
			}
			name := string(rresp[pos:nul])
			pos = nul + 1
			if name == "" || name == "." || name == ".." {
				continue
			}
			items = append(items, DirItem{
				Name:    name,
				IsDir:   (flags & 0x01) != 0,
				Size:    sz,
				ModTime: time.Unix(int64(mtime), 0),
			})
		}
		if (dirstat & 0x01) != 0 { // TNFS_DIRSTATUS_EOF
			break
		}
	}
	return items, nil
}

func (c *TNFSClient) listDirBasicLocked(cleanDir string, pattern string) ([]DirItem, error) {
	req := append([]byte(cleanDir), 0x00)
	resp, err := c.rpcLocked(tnfsCmdOpenDir, req)
	if err != nil {
		return nil, err
	}
	if len(resp) < 6 || resp[4] != 0x00 {
		return nil, fmt.Errorf("tnfs opendir failed")
	}
	dh := resp[5]
	defer func() {
		_, _ = c.rpcLocked(tnfsCmdCloseDir, []byte{dh})
	}()

	var items []DirItem
	for {
		rresp, err := c.rpcLocked(tnfsCmdReadDir, []byte{dh})
		if err != nil {
			return nil, err
		}
		if len(rresp) < 5 {
			break
		}
		if rresp[4] == tnfsErrEOF {
			break
		}
		if rresp[4] != 0x00 {
			return nil, fmt.Errorf("tnfs readdir status 0x%02X", rresp[4])
		}
		rawName := rresp[5:]
		if nul := indexByteZero(rawName); nul >= 0 {
			rawName = rawName[:nul]
		}
		name := string(rawName)
		if name == "" || name == "." || name == ".." || strings.HasPrefix(name, ".") {
			continue
		}
		fullPath := path.Join(cleanDir, name)
		isDir, sz, mtime, _ := c.statLocked(fullPath)
		if !isDir && pattern != "" {
			if matched, _ := path.Match(strings.ToLower(pattern), strings.ToLower(name)); !matched {
				continue
			}
		}
		items = append(items, DirItem{
			Name:    name,
			IsDir:   isDir,
			Size:    sz,
			ModTime: mtime,
		})
	}

	sort.Slice(items, func(i, j int) bool {
		if items[i].IsDir != items[j].IsDir {
			return items[i].IsDir
		}
		return strings.ToLower(items[i].Name) < strings.ToLower(items[j].Name)
	})
	return items, nil
}

func (c *TNFSClient) statLocked(fullPath string) (isDir bool, size uint32, mtime time.Time, err error) {
	req := append([]byte(cleanTNFSPath(fullPath)), 0x00)
	resp, err := c.rpcLocked(tnfsCmdStat, req)
	if err != nil {
		return false, 0, time.Time{}, err
	}
	if len(resp) < 23 || resp[4] != 0x00 {
		code := byte(0xFF)
		if len(resp) >= 5 {
			code = resp[4]
		}
		return false, 0, time.Time{}, fmt.Errorf("tnfs stat error 0x%02X", code)
	}
	mode := binary.LittleEndian.Uint16(resp[5:7])
	size = binary.LittleEndian.Uint32(resp[11:15])
	mt := binary.LittleEndian.Uint32(resp[19:23])
	isDir = (mode & 0170000) == 0040000
	return isDir, size, time.Unix(int64(mt), 0), nil
}

// TNFSFile wraps an open remote disk image over TNFS with a small sector cache.
type TNFSFile struct {
	client   *TNFSClient
	fd       byte
	writable bool
	size     uint32
	cache    map[uint32][256]byte
}

func (c *TNFSClient) OpenFile(filePath string, writable bool) (*TNFSFile, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	clean := cleanTNFSPath(filePath)
	_, sz, _, statErr := c.statLocked(clean)

	flags := uint16(0x0001) // O_RDONLY
	if writable {
		flags = 0x0003 // O_RDWR
	}
	req := make([]byte, 4+len(clean)+1)
	binary.LittleEndian.PutUint16(req[0:2], flags)
	binary.LittleEndian.PutUint16(req[2:4], 0)
	copy(req[4:], clean)
	req[4+len(clean)] = 0

	resp, err := c.rpcLocked(tnfsCmdOpen, req)
	if err != nil {
		return nil, err
	}
	if len(resp) < 6 || resp[4] != 0x00 {
		code := byte(0xFF)
		if len(resp) >= 5 {
			code = resp[4]
		}
		return nil, fmt.Errorf("tnfs open %q failed (status 0x%02X)", clean, code)
	}
	if statErr != nil {
		sz = 0xFFFFFFFF
	}
	return &TNFSFile{
		client:   c,
		fd:       resp[5],
		writable: writable,
		size:     sz,
		cache:    make(map[uint32][256]byte),
	}, nil
}

func (f *TNFSFile) ReadSector(lsn uint32) ([256]byte, byte) {
	var zero [256]byte
	f.client.mu.Lock()
	defer f.client.mu.Unlock()

	if sec, ok := f.cache[lsn]; ok {
		return sec, 0x00
	}
	off := uint64(lsn) * 256
	if f.size != 0xFFFFFFFF && off >= uint64(f.size) {
		return zero, 211 // DriveWire EOF
	}

	var seekReq [6]byte
	seekReq[0] = f.fd
	seekReq[1] = 0 // SEEK_SET
	binary.LittleEndian.PutUint32(seekReq[2:6], uint32(off))
	sresp, err := f.client.rpcLocked(tnfsCmdLseek, seekReq[:])
	if err != nil || len(sresp) < 5 || sresp[4] != 0x00 {
		return zero, 0xF4
	}

	var readReq [3]byte
	readReq[0] = f.fd
	binary.LittleEndian.PutUint16(readReq[1:3], 256)
	rresp, err := f.client.rpcLocked(tnfsCmdRead, readReq[:])
	if err != nil || len(rresp) < 5 {
		return zero, 0xF4
	}
	if rresp[4] == tnfsErrEOF {
		return zero, 211
	}
	if rresp[4] != 0x00 || len(rresp) < 7 {
		return zero, 0xF4
	}
	got := int(binary.LittleEndian.Uint16(rresp[5:7]))
	if 7+got > len(rresp) {
		got = len(rresp) - 7
	}
	var sec [256]byte
	copy(sec[:], rresp[7:7+got])
	if len(f.cache) >= 256 {
		clear(f.cache)
	}
	f.cache[lsn] = sec
	return sec, 0x00
}

func (f *TNFSFile) WriteSector(lsn uint32, sec [256]byte) byte {
	if !f.writable {
		return 0xF5
	}
	f.client.mu.Lock()
	defer f.client.mu.Unlock()

	off := uint64(lsn) * 256
	var seekReq [6]byte
	seekReq[0] = f.fd
	seekReq[1] = 0 // SEEK_SET
	binary.LittleEndian.PutUint32(seekReq[2:6], uint32(off))
	sresp, err := f.client.rpcLocked(tnfsCmdLseek, seekReq[:])
	if err != nil || len(sresp) < 5 || sresp[4] != 0x00 {
		return 0xF5
	}

	writeReq := make([]byte, 3+256)
	writeReq[0] = f.fd
	binary.LittleEndian.PutUint16(writeReq[1:3], 256)
	copy(writeReq[3:], sec[:])
	wresp, err := f.client.rpcLocked(tnfsCmdWrite, writeReq)
	if err != nil || len(wresp) < 5 || wresp[4] != 0x00 {
		return 0xF5
	}
	f.cache[lsn] = sec
	if f.size != 0xFFFFFFFF && uint32(off+256) > f.size {
		f.size = uint32(off + 256)
	}
	return 0x00
}

func (f *TNFSFile) Close() {
	f.client.mu.Lock()
	defer f.client.mu.Unlock()
	if f.client.conn != nil {
		_, _ = f.client.rpcLocked(tnfsCmdClose, []byte{f.fd})
	}
}

func indexByteZero(b []byte) int {
	for i, v := range b {
		if v == 0 {
			return i
		}
	}
	return -1
}
