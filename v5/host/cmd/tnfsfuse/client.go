package main

import (
	"bufio"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"path"
	"sort"
	"strings"
	"sync"
	"syscall"
	"time"
)

const (
	defaultTNFSPort = "16384"

	tnfsCmdMount    = 0x00
	tnfsCmdUmount   = 0x01
	tnfsCmdOpenDir  = 0x10
	tnfsCmdReadDir  = 0x11
	tnfsCmdCloseDir = 0x12
	tnfsCmdOpenDirX = 0x17
	tnfsCmdReadDirX = 0x18
	tnfsCmdRead     = 0x21
	tnfsCmdClose    = 0x23
	tnfsCmdStat     = 0x24
	tnfsCmdLseek    = 0x25
	tnfsCmdOpen     = 0x29

	tnfsOK             = 0x00
	tnfsErrPerm        = 0x01
	tnfsErrNoEnt       = 0x02
	tnfsErrIO          = 0x03
	tnfsErrBadFD       = 0x06
	tnfsErrAgain       = 0x07
	tnfsErrAccess      = 0x09
	tnfsErrExist       = 0x0B
	tnfsErrNotDir      = 0x0C
	tnfsErrIsDir       = 0x0D
	tnfsErrInvalidArg  = 0x0E
	tnfsErrReadOnlyFS  = 0x14
	tnfsErrNameTooLong = 0x15
	tnfsErrUnimpl      = 0x16
	tnfsErrEOF         = 0x21

	tnfsDirEntryDir = 0x01
	tnfsDirStatEOF  = 0x01
	tnfsMaxReadSize = 4096
)

func normalizeServerAddr(server string) string {
	s := strings.TrimSpace(server)
	s = strings.TrimPrefix(strings.TrimPrefix(s, "tnfs://"), "TNFS://")
	if idx := strings.IndexByte(s, '/'); idx >= 0 {
		s = s[:idx]
	}
	if !strings.Contains(s, ":") {
		s += ":" + defaultTNFSPort
	}
	return s
}

type tnfsStatInfo struct {
	Name  string
	IsDir bool
	Mode  uint32
	Size  uint64
	ATime uint64
	MTime uint64
	CTime uint64
}

type tnfsError struct {
	op   string
	code byte
}

func (e *tnfsError) Error() string {
	return fmt.Sprintf("tnfs %s failed (status 0x%02X)", e.op, e.code)
}

func tnfsCodeToErrno(code byte) syscall.Errno {
	switch code {
	case tnfsOK:
		return 0
	case tnfsErrPerm:
		return syscall.EPERM
	case tnfsErrNoEnt:
		return syscall.ENOENT
	case tnfsErrIO:
		return syscall.EIO
	case tnfsErrBadFD:
		return syscall.EBADF
	case tnfsErrAgain:
		return syscall.EAGAIN
	case tnfsErrAccess:
		return syscall.EACCES
	case tnfsErrExist:
		return syscall.EEXIST
	case tnfsErrNotDir:
		return syscall.ENOTDIR
	case tnfsErrIsDir:
		return syscall.EISDIR
	case tnfsErrInvalidArg:
		return syscall.EINVAL
	case tnfsErrReadOnlyFS:
		return syscall.EROFS
	case tnfsErrNameTooLong:
		return syscall.ENAMETOOLONG
	case tnfsErrUnimpl:
		return syscall.ENOSYS
	default:
		return syscall.EIO
	}
}

func errToErrno(err error) syscall.Errno {
	if err == nil {
		return 0
	}
	var te *tnfsError
	if errors.As(err, &te) {
		return tnfsCodeToErrno(te.code)
	}
	var errno syscall.Errno
	if errors.As(err, &errno) {
		return errno
	}
	return syscall.EIO
}

// tnfsClient is a read-only TCP-only TNFS client.
type tnfsClient struct {
	mu      sync.Mutex
	addr    string
	conn    net.Conn
	r       *bufio.Reader
	sid     uint16
	seq     byte
	mounted bool
}

func dialTNFS(server string) (*tnfsClient, error) {
	c := &tnfsClient{
		addr: normalizeServerAddr(server),
	}
	if err := c.connectAndMountLocked(); err != nil {
		return nil, err
	}
	return c, nil
}

func (c *tnfsClient) connectAndMountLocked() error {
	if c.conn != nil {
		_ = c.conn.Close()
		c.conn = nil
		c.r = nil
		c.mounted = false
	}
	conn, err := net.DialTimeout("tcp", c.addr, 10*time.Second)
	if err != nil {
		return fmt.Errorf("tnfs tcp dial %s: %w", c.addr, err)
	}
	c.conn = conn
	c.r = bufio.NewReaderSize(conn, 16*1024)
	c.sid = 0
	c.seq = 0

	payload := []byte{0x02, 0x01, '/', 0x00, 0x00, 0x00}
	resp, status, err := c.rpcOnceLocked(tnfsCmdMount, payload)
	if err != nil {
		_ = conn.Close()
		c.conn = nil
		c.r = nil
		return fmt.Errorf("tnfs mount %s: %w", c.addr, err)
	}
	if status != tnfsOK {
		_ = conn.Close()
		c.conn = nil
		c.r = nil
		return &tnfsError{op: "mount", code: status}
	}
	_ = resp
	c.mounted = true
	return nil
}

func (c *tnfsClient) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.conn == nil {
		return nil
	}
	if c.mounted {
		_, _, _ = c.rpcOnceLocked(tnfsCmdUmount, nil)
		c.mounted = false
	}
	err := c.conn.Close()
	c.conn = nil
	c.r = nil
	return err
}

func (c *tnfsClient) rpcWithReconnectLocked(cmd byte, payload []byte) ([]byte, byte, error) {
	if c.conn == nil || !c.mounted {
		if err := c.connectAndMountLocked(); err != nil {
			return nil, 0, err
		}
	}
	body, status, err := c.rpcOnceLocked(cmd, payload)
	if err == nil {
		return body, status, nil
	}
	if err := c.connectAndMountLocked(); err != nil {
		return nil, 0, err
	}
	return c.rpcOnceLocked(cmd, payload)
}

func (c *tnfsClient) rpcOnceLocked(cmd byte, payload []byte) ([]byte, byte, error) {
	if c.conn == nil || c.r == nil {
		return nil, 0, errors.New("tnfs connection closed")
	}
	c.seq++
	seq := c.seq

	pkt := make([]byte, 4+len(payload))
	binary.LittleEndian.PutUint16(pkt[0:2], c.sid)
	pkt[2] = seq
	pkt[3] = cmd
	copy(pkt[4:], payload)

	_ = c.conn.SetDeadline(time.Now().Add(15 * time.Second))
	for total := 0; total < len(pkt); {
		n, err := c.conn.Write(pkt[total:])
		total += n
		if err != nil {
			return nil, 0, err
		}
	}

	var hdr [5]byte
	if _, err := io.ReadFull(c.r, hdr[:]); err != nil {
		return nil, 0, err
	}
	respSID := binary.LittleEndian.Uint16(hdr[0:2])
	if hdr[2] != seq || hdr[3] != cmd {
		return nil, 0, fmt.Errorf("tnfs response mismatch: got seq=%d cmd=0x%02X, want seq=%d cmd=0x%02X", hdr[2], hdr[3], seq, cmd)
	}
	if cmd == tnfsCmdMount {
		c.sid = respSID
	}
	status := hdr[4]
	if status != tnfsOK {
		if cmd == tnfsCmdMount && c.r.Buffered() >= 2 {
			_, _ = io.CopyN(io.Discard, c.r, 2)
		} else if status == tnfsErrAgain {
			var backoff [2]byte
			_, _ = io.ReadFull(c.r, backoff[:])
		}
		return nil, status, nil
	}

	body, err := readTNFSResponseBody(c.r, cmd)
	if err != nil {
		return nil, 0, err
	}
	return body, status, nil
}

func readTNFSResponseBody(r *bufio.Reader, cmd byte) ([]byte, error) {
	switch cmd {
	case tnfsCmdMount:
		var buf [4]byte
		_, err := io.ReadFull(r, buf[:])
		return buf[:], err

	case tnfsCmdUmount, tnfsCmdCloseDir, tnfsCmdClose:
		return nil, nil

	case tnfsCmdOpenDir, tnfsCmdOpen:
		b, err := r.ReadByte()
		if err != nil {
			return nil, err
		}
		return []byte{b}, nil

	case tnfsCmdReadDir:
		s, err := r.ReadBytes(0)
		if err != nil {
			return nil, err
		}
		return s[:len(s)-1], nil

	case tnfsCmdOpenDirX:
		var buf [3]byte
		_, err := io.ReadFull(r, buf[:])
		return buf[:], err

	case tnfsCmdReadDirX:
		var hdr [4]byte
		if _, err := io.ReadFull(r, hdr[:]); err != nil {
			return nil, err
		}
		out := make([]byte, 0, 128)
		out = append(out, hdr[:]...)
		count := int(hdr[0])
		for i := 0; i < count; i++ {
			var fixed [13]byte
			if _, err := io.ReadFull(r, fixed[:]); err != nil {
				return nil, err
			}
			name, err := r.ReadBytes(0)
			if err != nil {
				return nil, err
			}
			out = append(out, fixed[:]...)
			out = append(out, name...)
		}
		return out, nil

	case tnfsCmdStat:
		var fixed [22]byte
		if _, err := io.ReadFull(r, fixed[:]); err != nil {
			return nil, err
		}
		if _, err := r.ReadBytes(0); err != nil { // uidstring
			return nil, err
		}
		if _, err := r.ReadBytes(0); err != nil { // gidstring
			return nil, err
		}
		return fixed[:], nil

	case tnfsCmdLseek:
		var buf [4]byte
		_, err := io.ReadFull(r, buf[:])
		return buf[:], err

	case tnfsCmdRead:
		var szBuf [2]byte
		if _, err := io.ReadFull(r, szBuf[:]); err != nil {
			return nil, err
		}
		n := int(binary.LittleEndian.Uint16(szBuf[:]))
		data := make([]byte, n)
		if _, err := io.ReadFull(r, data); err != nil {
			return nil, err
		}
		return data, nil

	default:
		return nil, fmt.Errorf("unexpected tnfs cmd 0x%02X", cmd)
	}
}

func cleanTNFSPath(p string) string {
	cleaned := path.Clean("/" + strings.TrimSpace(p))
	if cleaned == "" {
		return "/"
	}
	return cleaned
}

func (c *tnfsClient) Stat(rawPath string) (tnfsStatInfo, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.statLocked(cleanTNFSPath(rawPath))
}

func (c *tnfsClient) statLocked(cleanPath string) (tnfsStatInfo, error) {
	req := append([]byte(cleanPath), 0x00)
	body, status, err := c.rpcWithReconnectLocked(tnfsCmdStat, req)
	if err != nil {
		return tnfsStatInfo{}, err
	}
	if status != tnfsOK {
		return tnfsStatInfo{}, &tnfsError{op: "stat", code: status}
	}
	rawMode := binary.LittleEndian.Uint16(body[0:2])
	sz := binary.LittleEndian.Uint32(body[6:10])
	at := binary.LittleEndian.Uint32(body[10:14])
	mt := binary.LittleEndian.Uint32(body[14:18])
	ct := binary.LittleEndian.Uint32(body[18:22])

	isDir := (rawMode & 0170000) == 0040000
	perm := uint32(0444)
	if isDir {
		perm = 0555
	}
	return tnfsStatInfo{
		Name:  path.Base(cleanPath),
		IsDir: isDir,
		Mode:  perm,
		Size:  uint64(sz),
		ATime: uint64(at),
		MTime: uint64(mt),
		CTime: uint64(ct),
	}, nil
}

func (c *tnfsClient) ListDir(rawPath string) ([]tnfsStatInfo, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	cleanDir := cleanTNFSPath(rawPath)
	if items, err := c.listDirXLocked(cleanDir); err == nil {
		return items, nil
	}
	return c.listDirBasicLocked(cleanDir)
}

func (c *tnfsClient) listDirXLocked(cleanDir string) ([]tnfsStatInfo, error) {
	var req []byte
	req = append(req, 0x00, 0x00, 0x00, 0x00, 0x00) // opts=0, sort=0, max=0, empty pattern
	req = append(req, []byte(cleanDir)...)
	req = append(req, 0x00)

	body, status, err := c.rpcWithReconnectLocked(tnfsCmdOpenDirX, req)
	if err != nil {
		return nil, err
	}
	if status != tnfsOK || len(body) < 1 {
		return nil, &tnfsError{op: "opendirx", code: status}
	}
	dh := body[0]
	defer func() {
		_, _, _ = c.rpcOnceLocked(tnfsCmdCloseDir, []byte{dh})
	}()

	var items []tnfsStatInfo
	for {
		rbody, rstat, err := c.rpcOnceLocked(tnfsCmdReadDirX, []byte{dh, 0})
		if err != nil {
			return nil, err
		}
		if rstat == tnfsErrEOF {
			break
		}
		if rstat != tnfsOK {
			return nil, &tnfsError{op: "readdirx", code: rstat}
		}
		count := int(rbody[0])
		dirstat := rbody[1]
		pos := 4
		for i := 0; i < count && pos+13 < len(rbody); i++ {
			flags := rbody[pos]
			sz := binary.LittleEndian.Uint32(rbody[pos+1 : pos+5])
			mt := binary.LittleEndian.Uint32(rbody[pos+5 : pos+9])
			ct := binary.LittleEndian.Uint32(rbody[pos+9 : pos+13])
			pos += 13
			nul := pos
			for nul < len(rbody) && rbody[nul] != 0 {
				nul++
			}
			name := string(rbody[pos:nul])
			pos = nul + 1
			if name == "" || name == "." || name == ".." {
				continue
			}
			isDir := (flags & tnfsDirEntryDir) != 0
			perm := uint32(0444)
			if isDir {
				perm = 0555
			}
			items = append(items, tnfsStatInfo{
				Name:  name,
				IsDir: isDir,
				Mode:  perm,
				Size:  uint64(sz),
				MTime: uint64(mt),
				CTime: uint64(ct),
				ATime: uint64(mt),
			})
		}
		if (dirstat & tnfsDirStatEOF) != 0 {
			break
		}
	}
	return items, nil
}

func (c *tnfsClient) listDirBasicLocked(cleanDir string) ([]tnfsStatInfo, error) {
	req := append([]byte(cleanDir), 0x00)
	body, status, err := c.rpcWithReconnectLocked(tnfsCmdOpenDir, req)
	if err != nil {
		return nil, err
	}
	if status != tnfsOK || len(body) < 1 {
		return nil, &tnfsError{op: "opendir", code: status}
	}
	dh := body[0]
	defer func() {
		_, _, _ = c.rpcOnceLocked(tnfsCmdCloseDir, []byte{dh})
	}()

	var items []tnfsStatInfo
	for {
		rbody, rstat, err := c.rpcOnceLocked(tnfsCmdReadDir, []byte{dh})
		if err != nil {
			return nil, err
		}
		if rstat == tnfsErrEOF {
			break
		}
		if rstat != tnfsOK {
			return nil, &tnfsError{op: "readdir", code: rstat}
		}
		name := string(rbody)
		if name == "" || name == "." || name == ".." {
			continue
		}
		st, statErr := c.statLocked(path.Join(cleanDir, name))
		if statErr != nil {
			st = tnfsStatInfo{Name: name, Mode: 0444}
		} else {
			st.Name = name
		}
		items = append(items, st)
	}
	sort.Slice(items, func(i, j int) bool {
		return items[i].Name < items[j].Name
	})
	return items, nil
}

func (c *tnfsClient) OpenReadOnly(rawPath string) (byte, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	clean := cleanTNFSPath(rawPath)
	req := make([]byte, 4+len(clean)+1)
	binary.LittleEndian.PutUint16(req[0:2], 0x0001) // O_RDONLY
	copy(req[4:], clean)

	body, status, err := c.rpcWithReconnectLocked(tnfsCmdOpen, req)
	if err != nil {
		return 0, err
	}
	if status != tnfsOK || len(body) < 1 {
		return 0, &tnfsError{op: "open", code: status}
	}
	return body[0], nil
}

func (c *tnfsClient) ReadAt(fd byte, dst []byte, off int64) (int, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if off < 0 {
		return 0, syscall.EINVAL
	}
	var seekReq [6]byte
	seekReq[0] = fd
	seekReq[1] = 0 // SEEK_SET
	binary.LittleEndian.PutUint32(seekReq[2:6], uint32(off))
	_, sstat, err := c.rpcOnceLocked(tnfsCmdLseek, seekReq[:])
	if err != nil {
		return 0, err
	}
	if sstat != tnfsOK {
		return 0, &tnfsError{op: "lseek", code: sstat}
	}

	total := 0
	for total < len(dst) {
		want := len(dst) - total
		if want > tnfsMaxReadSize {
			want = tnfsMaxReadSize
		}
		var readReq [3]byte
		readReq[0] = fd
		binary.LittleEndian.PutUint16(readReq[1:3], uint16(want))
		chunk, rstat, err := c.rpcOnceLocked(tnfsCmdRead, readReq[:])
		if err != nil {
			return total, err
		}
		if rstat == tnfsErrEOF {
			break
		}
		if rstat != tnfsOK {
			return total, &tnfsError{op: "read", code: rstat}
		}
		copy(dst[total:], chunk)
		total += len(chunk)
		if len(chunk) < want {
			break
		}
	}
	return total, nil
}

func (c *tnfsClient) CloseFile(fd byte) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.conn != nil {
		_, _, _ = c.rpcOnceLocked(tnfsCmdClose, []byte{fd})
	}
}
