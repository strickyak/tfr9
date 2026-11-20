package main

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"errors"
	"io"
	"log"
	"net"
	"os"
	"path"
	"strings"
	"sync"
	"sync/atomic"
)

const (
	tnfsCmdMount      = 0x00
	tnfsCmdUmount     = 0x01
	tnfsCmdOpenDir    = 0x10
	tnfsCmdReadDir    = 0x11
	tnfsCmdCloseDir   = 0x12
	tnfsCmdMkdir      = 0x13
	tnfsCmdRmdir      = 0x14
	tnfsCmdTellDir    = 0x15
	tnfsCmdSeekDir    = 0x16
	tnfsCmdOpenDirX   = 0x17
	tnfsCmdReadDirX   = 0x18
	tnfsCmdRead       = 0x21
	tnfsCmdWrite      = 0x22
	tnfsCmdClose      = 0x23
	tnfsCmdStat       = 0x24
	tnfsCmdLseek      = 0x25
	tnfsCmdUnlink     = 0x26
	tnfsCmdChmod      = 0x27
	tnfsCmdRename     = 0x28
	tnfsCmdOpen       = 0x29
	tnfsCmdSize       = 0x30
	tnfsCmdFree       = 0x31
	tnfsCmdSizeBytes  = 0x32
	tnfsCmdFreeBytes  = 0x33

	tnfsOK             = 0x00
	tnfsErrNoEnt       = 0x02
	tnfsErrIO          = 0x03
	tnfsErrBadFD       = 0x06
	tnfsErrNotDir      = 0x0C
	tnfsErrIsDir       = 0x0D
	tnfsErrInvalidArg  = 0x0E
	tnfsErrTooManyOpen = 0x10
	tnfsErrReadOnlyFS  = 0x14
	tnfsErrUnimpl      = 0x16
	tnfsErrEOF         = 0x21
	tnfsErrInvalidHnd  = 0xFF

	tnfsDirOptNoFoldersFirst = 0x01
	tnfsDirOptDirPattern     = 0x08
	tnfsDirOptNoFolders      = 0x20

	tnfsDirSortNone       = 0x01
	tnfsDirSortCase       = 0x02
	tnfsDirSortDescending = 0x04
	tnfsDirSortModified   = 0x08
	tnfsDirSortSize       = 0x10

	tnfsDirEntryDir = 0x01
	tnfsDirStatEOF  = 0x01

	tnfsMaxPayload = 528

	tnfsOpenWriteMask = 0x0002 | 0x0008 | 0x0100 | 0x0200 | 0x0400 // O_WRONLY/RDWR(bit1), APPEND, CREAT, TRUNC, EXCL
)

type dirHandle struct {
	entries []fsEntry
	pos     int
}

type session struct {
	id        uint16
	mountRoot string
	dirs      map[byte]*dirHandle
	files     map[byte]*os.File
	nextDH    byte
	nextFD    byte
}

func newSession(id uint16, mountRoot string) *session {
	return &session{
		id:        id,
		mountRoot: mountRoot,
		dirs:      make(map[byte]*dirHandle),
		files:     make(map[byte]*os.File),
		nextDH:    1,
		nextFD:    1,
	}
}

func (s *session) closeAll() {
	for _, f := range s.files {
		_ = f.Close()
	}
	clear(s.files)
	clear(s.dirs)
}

func (s *session) allocDir(entries []fsEntry) (byte, bool) {
	if len(s.dirs) >= 254 {
		return 0, false
	}
	for {
		dh := s.nextDH
		s.nextDH++
		if s.nextDH == 0 {
			s.nextDH = 1
		}
		if _, used := s.dirs[dh]; !used {
			s.dirs[dh] = &dirHandle{entries: entries}
			return dh, true
		}
	}
}

func (s *session) allocFile(f *os.File) (byte, bool) {
	if len(s.files) >= 254 {
		return 0, false
	}
	for {
		fd := s.nextFD
		s.nextFD++
		if s.nextFD == 0 {
			s.nextFD = 1
		}
		if _, used := s.files[fd]; !used {
			s.files[fd] = f
			return fd, true
		}
	}
}

type tnfsServer struct {
	fsys    *readOnlyFS
	verbose bool
	nextSID atomic.Uint32

	mu       sync.Mutex
	sessions map[uint16]*session
}

func newTNFSServer(fsys *readOnlyFS, verbose bool) *tnfsServer {
	s := &tnfsServer{
		fsys:     fsys,
		verbose:  verbose,
		sessions: make(map[uint16]*session),
	}
	s.nextSID.Store(1)
	return s
}

func (s *tnfsServer) mintSID() uint16 {
	for {
		v := uint16(s.nextSID.Add(1))
		if v == 0 {
			continue
		}
		s.mu.Lock()
		_, exists := s.sessions[v]
		s.mu.Unlock()
		if !exists {
			return v
		}
	}
}

func (s *tnfsServer) getSession(sid uint16) *session {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.sessions[sid]
}

func (s *tnfsServer) serveConn(conn net.Conn) {
	defer conn.Close()
	r := bufio.NewReader(conn)
	var connSIDs []uint16

	defer func() {
		s.mu.Lock()
		for _, sid := range connSIDs {
			if sess, ok := s.sessions[sid]; ok {
				sess.closeAll()
				delete(s.sessions, sid)
			}
		}
		s.mu.Unlock()
	}()

	var hdr [4]byte
	for {
		if _, err := io.ReadFull(r, hdr[:]); err != nil {
			if !errors.Is(err, io.EOF) && s.verbose {
				log.Printf("tnfsd %s read error: %v", conn.RemoteAddr(), err)
			}
			return
		}
		sid := binary.LittleEndian.Uint16(hdr[0:2])
		seq := hdr[2]
		cmd := hdr[3]

		respSID, payload, err := s.handleCommand(r, sid, cmd, &connSIDs)
		if err != nil {
			if s.verbose {
				log.Printf("tnfsd %s cmd=0x%02X decode error: %v", conn.RemoteAddr(), cmd, err)
			}
			return
		}

		out := make([]byte, 4+len(payload))
		binary.LittleEndian.PutUint16(out[0:2], respSID)
		out[2] = seq
		out[3] = cmd
		copy(out[4:], payload)
		if _, err := writeAll(conn, out); err != nil {
			return
		}
	}
}

func writeAll(w io.Writer, data []byte) (int, error) {
	total := 0
	for total < len(data) {
		n, err := w.Write(data[total:])
		total += n
		if err != nil {
			return total, err
		}
	}
	return total, nil
}

func readCString(r *bufio.Reader) (string, error) {
	b, err := r.ReadBytes(0)
	if err != nil {
		return "", err
	}
	return string(b[:len(b)-1]), nil
}

func (s *tnfsServer) handleCommand(r *bufio.Reader, sid uint16, cmd byte, connSIDs *[]uint16) (uint16, []byte, error) {
	switch cmd {
	case tnfsCmdMount:
		var ver [2]byte
		if _, err := io.ReadFull(r, ver[:]); err != nil {
			return 0, nil, err
		}
		mountPath, err := readCString(r)
		if err != nil {
			return 0, nil, err
		}
		if _, err := readCString(r); err != nil { // user (ignored)
			return 0, nil, err
		}
		if _, err := readCString(r); err != nil { // password (ignored)
			return 0, nil, err
		}

		cleanMount := path.Clean("/" + strings.TrimSpace(mountPath))
		if _, _, code := s.fsys.resolve("/", cleanMount); code != tnfsOK {
			return 0, []byte{code, 0x02, 0x01}, nil
		}
		newSID := s.mintSID()
		sess := newSession(newSID, cleanMount)
		s.mu.Lock()
		s.sessions[newSID] = sess
		s.mu.Unlock()
		*connSIDs = append(*connSIDs, newSID)
		if s.verbose {
			log.Printf("tnfsd MOUNT %q -> sid=0x%04X", cleanMount, newSID)
		}
		// Status OK (0x00), TNFS version 1.2 (LSB=0x02, MSB=0x01), min_retry=100ms (0x64, 0x00).
		return newSID, []byte{tnfsOK, 0x02, 0x01, 0x64, 0x00}, nil

	case tnfsCmdUmount:
		s.mu.Lock()
		sess, ok := s.sessions[sid]
		if ok {
			sess.closeAll()
			delete(s.sessions, sid)
		}
		s.mu.Unlock()
		if !ok {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		return sid, []byte{tnfsOK}, nil

	case tnfsCmdOpenDir:
		dirPath, err := readCString(r)
		if err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		entries, code := s.fsys.listDir(sess.mountRoot, dirPath, tnfsDirOptNoFoldersFirst, tnfsDirSortNone, "", 0)
		if code != tnfsOK {
			return sid, []byte{code}, nil
		}
		dh, ok := sess.allocDir(entries)
		if !ok {
			return sid, []byte{tnfsErrTooManyOpen}, nil
		}
		return sid, []byte{tnfsOK, dh}, nil

	case tnfsCmdReadDir:
		dh, err := r.ReadByte()
		if err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		d, ok := sess.dirs[dh]
		if !ok {
			return sid, []byte{tnfsErrBadFD}, nil
		}
		if d.pos >= len(d.entries) {
			return sid, []byte{tnfsErrEOF}, nil
		}
		name := d.entries[d.pos].Name
		d.pos++
		out := make([]byte, 1+len(name)+1)
		out[0] = tnfsOK
		copy(out[1:], name)
		return sid, out, nil

	case tnfsCmdCloseDir:
		dh, err := r.ReadByte()
		if err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		if _, ok := sess.dirs[dh]; !ok {
			return sid, []byte{tnfsErrBadFD}, nil
		}
		delete(sess.dirs, dh)
		return sid, []byte{tnfsOK}, nil

	case tnfsCmdTellDir:
		dh, err := r.ReadByte()
		if err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		d, ok := sess.dirs[dh]
		if !ok {
			return sid, []byte{tnfsErrBadFD}, nil
		}
		var out [5]byte
		out[0] = tnfsOK
		binary.LittleEndian.PutUint32(out[1:5], uint32(d.pos))
		return sid, out[:], nil

	case tnfsCmdSeekDir:
		var req [5]byte
		if _, err := io.ReadFull(r, req[:]); err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		d, ok := sess.dirs[req[0]]
		if !ok {
			return sid, []byte{tnfsErrBadFD}, nil
		}
		pos := int(binary.LittleEndian.Uint32(req[1:5]))
		if pos < 0 || pos > len(d.entries) {
			return sid, []byte{tnfsErrInvalidArg}, nil
		}
		d.pos = pos
		return sid, []byte{tnfsOK}, nil

	case tnfsCmdOpenDirX:
		var hdr [4]byte
		if _, err := io.ReadFull(r, hdr[:]); err != nil {
			return sid, nil, err
		}
		pattern, err := readCString(r)
		if err != nil {
			return sid, nil, err
		}
		dirPath, err := readCString(r)
		if err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		maxResults := binary.LittleEndian.Uint16(hdr[2:4])
		entries, code := s.fsys.listDir(sess.mountRoot, dirPath, hdr[0], hdr[1], pattern, maxResults)
		if code != tnfsOK {
			return sid, []byte{code}, nil
		}
		dh, ok := sess.allocDir(entries)
		if !ok {
			return sid, []byte{tnfsErrTooManyOpen}, nil
		}
		var out [4]byte
		out[0] = tnfsOK
		out[1] = dh
		binary.LittleEndian.PutUint16(out[2:4], uint16(len(entries)))
		return sid, out[:], nil

	case tnfsCmdReadDirX:
		var req [2]byte
		if _, err := io.ReadFull(r, req[:]); err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		d, ok := sess.dirs[req[0]]
		if !ok {
			return sid, []byte{tnfsErrBadFD}, nil
		}
		if d.pos >= len(d.entries) {
			return sid, []byte{tnfsErrEOF}, nil
		}
		maxCount := int(req[1])
		firstPos := uint16(d.pos)
		var body bytes.Buffer
		count := 0
		for d.pos < len(d.entries) {
			if maxCount > 0 && count >= maxCount {
				break
			}
			ent := d.entries[d.pos]
			recLen := 13 + len(ent.Name) + 1
			if count > 0 && 5+body.Len()+recLen > tnfsMaxPayload {
				break
			}
			var flags byte
			if ent.IsDir {
				flags |= tnfsDirEntryDir
			}
			var rec [13]byte
			rec[0] = flags
			binary.LittleEndian.PutUint32(rec[1:5], ent.Size)
			binary.LittleEndian.PutUint32(rec[5:9], ent.MTime)
			binary.LittleEndian.PutUint32(rec[9:13], ent.CTime)
			body.Write(rec[:])
			body.WriteString(ent.Name)
			body.WriteByte(0)
			d.pos++
			count++
		}
		var dirstat byte
		if d.pos >= len(d.entries) {
			dirstat |= tnfsDirStatEOF
		}
		out := make([]byte, 5+body.Len())
		out[0] = tnfsOK
		out[1] = byte(count)
		out[2] = dirstat
		binary.LittleEndian.PutUint16(out[3:5], firstPos)
		copy(out[5:], body.Bytes())
		return sid, out, nil

	case tnfsCmdOpen:
		var hdr [4]byte
		if _, err := io.ReadFull(r, hdr[:]); err != nil {
			return sid, nil, err
		}
		filePath, err := readCString(r)
		if err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		flags := binary.LittleEndian.Uint16(hdr[0:2])
		if (flags & tnfsOpenWriteMask) != 0 {
			return sid, []byte{tnfsErrReadOnlyFS}, nil
		}
		f, code := s.fsys.openReadOnly(sess.mountRoot, filePath)
		if code != tnfsOK {
			return sid, []byte{code}, nil
		}
		fd, ok := sess.allocFile(f)
		if !ok {
			_ = f.Close()
			return sid, []byte{tnfsErrTooManyOpen}, nil
		}
		return sid, []byte{tnfsOK, fd}, nil

	case tnfsCmdRead:
		var req [3]byte
		if _, err := io.ReadFull(r, req[:]); err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		f, ok := sess.files[req[0]]
		if !ok {
			return sid, []byte{tnfsErrBadFD}, nil
		}
		want := int(binary.LittleEndian.Uint16(req[1:3]))
		buf := make([]byte, 3+want)
		n, err := f.Read(buf[3:])
		if n == 0 && errors.Is(err, io.EOF) {
			return sid, []byte{tnfsErrEOF}, nil
		}
		if err != nil && !errors.Is(err, io.EOF) {
			return sid, []byte{tnfsErrIO}, nil
		}
		buf[0] = tnfsOK
		binary.LittleEndian.PutUint16(buf[1:3], uint16(n))
		return sid, buf[:3+n], nil

	case tnfsCmdClose:
		fd, err := r.ReadByte()
		if err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		f, ok := sess.files[fd]
		if !ok {
			return sid, []byte{tnfsErrBadFD}, nil
		}
		delete(sess.files, fd)
		_ = f.Close()
		return sid, []byte{tnfsOK}, nil

	case tnfsCmdStat:
		filePath, err := readCString(r)
		if err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		ent, code := s.fsys.statEntry(sess.mountRoot, filePath)
		if code != tnfsOK {
			return sid, []byte{code}, nil
		}
		var out [25]byte
		out[0] = tnfsOK
		binary.LittleEndian.PutUint16(out[1:3], ent.Mode)
		binary.LittleEndian.PutUint32(out[7:11], ent.Size)
		binary.LittleEndian.PutUint32(out[11:15], ent.ATime)
		binary.LittleEndian.PutUint32(out[15:19], ent.MTime)
		binary.LittleEndian.PutUint32(out[19:23], ent.CTime)
		// out[23] = 0 (uidstring NUL), out[24] = 0 (gidstring NUL)
		return sid, out[:], nil

	case tnfsCmdLseek:
		var req [6]byte
		if _, err := io.ReadFull(r, req[:]); err != nil {
			return sid, nil, err
		}
		sess := s.getSession(sid)
		if sess == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		f, ok := sess.files[req[0]]
		if !ok {
			return sid, []byte{tnfsErrBadFD}, nil
		}
		whence := int(req[1])
		if whence < io.SeekStart || whence > io.SeekEnd {
			return sid, []byte{tnfsErrInvalidArg}, nil
		}
		offset := int32(binary.LittleEndian.Uint32(req[2:6]))
		newPos, err := f.Seek(int64(offset), whence)
		if err != nil {
			return sid, []byte{tnfsErrInvalidArg}, nil
		}
		var out [5]byte
		out[0] = tnfsOK
		binary.LittleEndian.PutUint32(out[1:5], uint32(newPos))
		return sid, out[:], nil

	case tnfsCmdSize:
		if s.getSession(sid) == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		var out [5]byte
		out[0] = tnfsOK
		binary.LittleEndian.PutUint32(out[1:5], 0x00100000) // 1 GiB nominal
		return sid, out[:], nil

	case tnfsCmdFree:
		if s.getSession(sid) == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		var out [5]byte
		out[0] = tnfsOK // 0 KB free (read-only)
		return sid, out[:], nil

	case tnfsCmdSizeBytes:
		if s.getSession(sid) == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		var out [9]byte
		out[0] = tnfsOK
		binary.LittleEndian.PutUint64(out[1:9], 0x40000000)
		return sid, out[:], nil

	case tnfsCmdFreeBytes:
		if s.getSession(sid) == nil {
			return sid, []byte{tnfsErrInvalidHnd}, nil
		}
		var out [9]byte
		out[0] = tnfsOK // 0 bytes free (read-only)
		return sid, out[:], nil

	case tnfsCmdWrite:
		var req [3]byte
		if _, err := io.ReadFull(r, req[:]); err != nil {
			return sid, nil, err
		}
		sz := int(binary.LittleEndian.Uint16(req[1:3]))
		if sz > 0 {
			if _, err := io.CopyN(io.Discard, r, int64(sz)); err != nil {
				return sid, nil, err
			}
		}
		return sid, []byte{tnfsErrReadOnlyFS}, nil

	case tnfsCmdMkdir, tnfsCmdRmdir, tnfsCmdUnlink:
		if _, err := readCString(r); err != nil {
			return sid, nil, err
		}
		return sid, []byte{tnfsErrReadOnlyFS}, nil

	case tnfsCmdChmod:
		var mode [2]byte
		if _, err := io.ReadFull(r, mode[:]); err != nil {
			return sid, nil, err
		}
		if _, err := readCString(r); err != nil {
			return sid, nil, err
		}
		return sid, []byte{tnfsErrReadOnlyFS}, nil

	case tnfsCmdRename:
		if _, err := readCString(r); err != nil {
			return sid, nil, err
		}
		if _, err := readCString(r); err != nil {
			return sid, nil, err
		}
		return sid, []byte{tnfsErrReadOnlyFS}, nil

	default:
		return sid, []byte{tnfsErrUnimpl}, nil
	}
}
