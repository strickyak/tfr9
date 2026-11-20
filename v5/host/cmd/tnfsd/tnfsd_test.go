package main

import (
	"bytes"
	"encoding/binary"
	"io"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

type testClient struct {
	t    *testing.T
	conn net.Conn
	sid  uint16
	seq  byte
}

func startTestServer(t *testing.T, root string) (*testClient, func()) {
	t.Helper()
	fsys, err := newReadOnlyFS(root)
	if err != nil {
		t.Fatalf("newReadOnlyFS: %v", err)
	}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	srv := newTNFSServer(fsys, false)
	go func() {
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			go srv.serveConn(c)
		}
	}()
	conn, err := net.Dial("tcp", ln.Addr().String())
	if err != nil {
		_ = ln.Close()
		t.Fatalf("dial: %v", err)
	}
	cleanup := func() {
		_ = conn.Close()
		_ = ln.Close()
	}
	return &testClient{t: t, conn: conn}, cleanup
}

func (c *testClient) rpc(cmd byte, payload []byte, expectLen int) []byte {
	c.t.Helper()
	c.seq++
	req := make([]byte, 4+len(payload))
	binary.LittleEndian.PutUint16(req[0:2], c.sid)
	req[2] = c.seq
	req[3] = cmd
	copy(req[4:], payload)
	if _, err := writeAll(c.conn, req); err != nil {
		c.t.Fatalf("write cmd 0x%02X: %v", cmd, err)
	}
	buf := make([]byte, 4096)
	n, err := c.conn.Read(buf)
	if err != nil {
		c.t.Fatalf("read cmd 0x%02X: %v", cmd, err)
	}
	if n < expectLen {
		c.t.Fatalf("short reply for cmd 0x%02X: got %d, want >= %d", cmd, n, expectLen)
	}
	if buf[2] != c.seq || buf[3] != cmd {
		c.t.Fatalf("header mismatch: got seq=%d cmd=0x%02X, want seq=%d cmd=0x%02X", buf[2], buf[3], c.seq, cmd)
	}
	return buf[:n]
}

func TestReadOnlyFSRequiresValidDir(t *testing.T) {
	if _, err := newReadOnlyFS(""); err == nil {
		t.Fatal("expected error for empty --fs")
	}
	f := filepath.Join(t.TempDir(), "notadir")
	if err := os.WriteFile(f, []byte("x"), 0644); err != nil {
		t.Fatal(err)
	}
	if _, err := newReadOnlyFS(f); err == nil {
		t.Fatal("expected error when --fs is a regular file")
	}
}

func TestNormalizeListenAddr(t *testing.T) {
	cases := []struct {
		in, want string
	}{
		{"localhost:16384", "localhost:16384"},
		{"localhost", "localhost:16384"},
		{"0.0.0.0", "0.0.0.0:16384"},
		{":9999", ":9999"},
		{"", ":16384"},
	}
	for _, tc := range cases {
		if got := normalizeListenAddr(tc.in); got != tc.want {
			t.Errorf("normalizeListenAddr(%q) = %q, want %q", tc.in, got, tc.want)
		}
	}
}

func TestTNFSReadOnlyTCPProtocol(t *testing.T) {
	dir := t.TempDir()
	sub := filepath.Join(dir, "sub")
	if err := os.Mkdir(sub, 0755); err != nil {
		t.Fatal(err)
	}
	helloData := []byte("Hello, TNFS over TCP!\n0123456789ABCDEF")
	if err := os.WriteFile(filepath.Join(dir, "hello.txt"), helloData, 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(sub, "disk.dsk"), []byte("DISKIMG"), 0644); err != nil {
		t.Fatal(err)
	}
	// Hidden dotfile and dotdir (must be ignored).
	if err := os.WriteFile(filepath.Join(dir, ".secret"), []byte("hidden"), 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.Mkdir(filepath.Join(dir, ".git"), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, ".git", "config"), []byte("nope"), 0644); err != nil {
		t.Fatal(err)
	}
	// Symlinks (must be ignored).
	if err := os.Symlink(filepath.Join(dir, "hello.txt"), filepath.Join(dir, "sym.txt")); err != nil {
		t.Logf("skipping symlink setup: %v", err)
	}
	if err := os.Symlink(sub, filepath.Join(dir, "symdir")); err != nil {
		t.Logf("skipping dir symlink setup: %v", err)
	}
	// Unreadable file (must be ignored when non-root).
	unreadPath := filepath.Join(dir, "unreadable.txt")
	if err := os.WriteFile(unreadPath, []byte("secret"), 0000); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = os.Chmod(unreadPath, 0644) })

	c, cleanup := startTestServer(t, dir)
	defer cleanup()

	// 1. MOUNT without password.
	mountPayload := append([]byte{0x02, 0x01}, []byte("/\x00\x00\x00")...)
	resp := c.rpc(tnfsCmdMount, mountPayload, 9)
	if resp[4] != tnfsOK {
		t.Fatalf("mount failed: 0x%02X", resp[4])
	}
	c.sid = binary.LittleEndian.Uint16(resp[0:2])
	if c.sid == 0 {
		t.Fatal("expected non-zero session ID")
	}

	// 2. OPENDIRX / READDIRX on "/".
	var odx []byte
	odx = append(odx, 0x00, 0x00, 0x00, 0x00) // default opts, sort, unlimited
	odx = append(odx, 0x00)                   // empty wildcard
	odx = append(odx, []byte("/\x00")...)
	resp = c.rpc(tnfsCmdOpenDirX, odx, 8)
	if resp[4] != tnfsOK {
		t.Fatalf("opendirx failed: 0x%02X", resp[4])
	}
	dh := resp[5]
	count := binary.LittleEndian.Uint16(resp[6:8])

	expectTwo := runtime.GOOS != "windows" && os.Geteuid() != 0
	if expectTwo && count != 2 {
		t.Fatalf("expected exactly 2 visible entries (sub, hello.txt), got %d", count)
	}

	resp = c.rpc(tnfsCmdReadDirX, []byte{dh, 0}, 9)
	if resp[4] != tnfsOK || resp[5] < 2 || (resp[6]&tnfsDirStatEOF) == 0 {
		t.Fatalf("readdirx unexpected: status=0x%02X count=%d dirstat=0x%02X", resp[4], resp[5], resp[6])
	}
	c.rpc(tnfsCmdCloseDir, []byte{dh}, 5)

	// 3. OPENDIR / READDIR / TELLDIR / SEEKDIR / CLOSEDIR.
	resp = c.rpc(tnfsCmdOpenDir, []byte("/\x00"), 6)
	if resp[4] != tnfsOK {
		t.Fatalf("opendir failed: 0x%02X", resp[4])
	}
	dh = resp[5]
	var names []string
	for {
		r := c.rpc(tnfsCmdReadDir, []byte{dh}, 5)
		if r[4] == tnfsErrEOF {
			break
		}
		if r[4] != tnfsOK {
			t.Fatalf("readdir error: 0x%02X", r[4])
		}
		nm := string(bytes.TrimRight(r[5:], "\x00"))
		names = append(names, nm)
	}
	if expectTwo && len(names) != 2 {
		t.Fatalf("readdir names=%v, want 2 entries", names)
	}
	// Seek back to 0 and re-read first entry.
	resp = c.rpc(tnfsCmdSeekDir, []byte{dh, 0, 0, 0, 0}, 5)
	if resp[4] != tnfsOK {
		t.Fatalf("seekdir failed: 0x%02X", resp[4])
	}
	resp = c.rpc(tnfsCmdTellDir, []byte{dh}, 9)
	if resp[4] != tnfsOK || binary.LittleEndian.Uint32(resp[5:9]) != 0 {
		t.Fatalf("telldir failed: %x", resp[4:])
	}
	c.rpc(tnfsCmdCloseDir, []byte{dh}, 5)

	// 4. Hidden / symlink / traversal / unreadable paths must all return ENOENT.
	for _, hidden := range []string{"/.secret", "/.git/config", "/sym.txt", "/symdir", "/symdir/disk.dsk", "/../etc/passwd"} {
		r := c.rpc(tnfsCmdStat, append([]byte(hidden), 0), 5)
		if r[4] != tnfsErrNoEnt {
			t.Fatalf("stat(%q) = 0x%02X, want ENOENT (0x02)", hidden, r[4])
		}
	}
	if expectTwo {
		r := c.rpc(tnfsCmdStat, []byte("/unreadable.txt\x00"), 5)
		if r[4] != tnfsErrNoEnt {
			t.Fatalf("stat(/unreadable.txt) = 0x%02X, want ENOENT", r[4])
		}
	}

	// 5. STAT / OPEN / LSEEK / READ / CLOSE on /hello.txt.
	resp = c.rpc(tnfsCmdStat, []byte("/hello.txt\x00"), 27)
	if resp[4] != tnfsOK {
		t.Fatalf("stat(/hello.txt) failed: 0x%02X", resp[4])
	}
	sz := binary.LittleEndian.Uint32(resp[11:15])
	if int(sz) != len(helloData) {
		t.Fatalf("stat size=%d, want %d", sz, len(helloData))
	}

	openReq := append([]byte{0x01, 0x00, 0x00, 0x00}, []byte("/hello.txt\x00")...)
	resp = c.rpc(tnfsCmdOpen, openReq, 6)
	if resp[4] != tnfsOK {
		t.Fatalf("open failed: 0x%02X", resp[4])
	}
	fd := resp[5]

	// Seek to offset 7 and read remaining bytes.
	var seekReq [6]byte
	seekReq[0] = fd
	seekReq[1] = byte(io.SeekStart)
	binary.LittleEndian.PutUint32(seekReq[2:6], 7)
	resp = c.rpc(tnfsCmdLseek, seekReq[:], 9)
	if resp[4] != tnfsOK || binary.LittleEndian.Uint32(resp[5:9]) != 7 {
		t.Fatalf("lseek failed: %x", resp[4:])
	}

	var readReq [3]byte
	readReq[0] = fd
	binary.LittleEndian.PutUint16(readReq[1:3], 64)
	resp = c.rpc(tnfsCmdRead, readReq[:], 7)
	if resp[4] != tnfsOK {
		t.Fatalf("read failed: 0x%02X", resp[4])
	}
	gotLen := int(binary.LittleEndian.Uint16(resp[5:7]))
	if gotData := resp[7 : 7+gotLen]; !bytes.Equal(gotData, helloData[7:]) {
		t.Fatalf("read data=%q, want %q", gotData, helloData[7:])
	}
	// Next read hits EOF.
	resp = c.rpc(tnfsCmdRead, readReq[:], 5)
	if resp[4] != tnfsErrEOF {
		t.Fatalf("expected EOF, got 0x%02X", resp[4])
	}
	c.rpc(tnfsCmdClose, []byte{fd}, 5)

	// 6. Write/mutation commands are rejected with EROFS and keep stream aligned.
	openWriteReq := append([]byte{0x03, 0x00, 0x00, 0x00}, []byte("/hello.txt\x00")...)
	if r := c.rpc(tnfsCmdOpen, openWriteReq, 5); r[4] != tnfsErrReadOnlyFS {
		t.Fatalf("O_RDWR open returned 0x%02X, want EROFS", r[4])
	}
	writeReq := append([]byte{1, 4, 0}, []byte("abcd")...)
	if r := c.rpc(tnfsCmdWrite, writeReq, 5); r[4] != tnfsErrReadOnlyFS {
		t.Fatalf("WRITE returned 0x%02X, want EROFS", r[4])
	}
	if r := c.rpc(tnfsCmdMkdir, []byte("/newdir\x00"), 5); r[4] != tnfsErrReadOnlyFS {
		t.Fatalf("MKDIR returned 0x%02X, want EROFS", r[4])
	}
	if r := c.rpc(tnfsCmdUnlink, []byte("/hello.txt\x00"), 5); r[4] != tnfsErrReadOnlyFS {
		t.Fatalf("UNLINK returned 0x%02X, want EROFS", r[4])
	}
	if r := c.rpc(tnfsCmdRename, []byte("/hello.txt\x00/x.txt\x00"), 5); r[4] != tnfsErrReadOnlyFS {
		t.Fatalf("RENAME returned 0x%02X, want EROFS", r[4])
	}

	// Verify stream is still aligned after WRITE payload consumption.
	if r := c.rpc(tnfsCmdStat, []byte("/sub/disk.dsk\x00"), 27); r[4] != tnfsOK {
		t.Fatalf("post-write STAT failed: 0x%02X", r[4])
	}

	// 7. UMOUNT.
	if r := c.rpc(tnfsCmdUmount, nil, 5); r[4] != tnfsOK {
		t.Fatalf("umount failed: 0x%02X", r[4])
	}
}
