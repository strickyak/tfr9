//go:build !windows

package main

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"io"
	"net"
	"os"
	"path/filepath"
	"testing"
)

func TestNormalizeServerAddr(t *testing.T) {
	cases := []struct {
		in, want string
	}{
		{"apps.irata.com", "apps.irata.com:16384"},
		{"apps.irata.com:1234", "apps.irata.com:1234"},
		{"tnfs://tnfs.fujinet.online/COCO", "tnfs.fujinet.online:16384"},
		{"localhost:16384", "localhost:16384"},
	}
	for _, tc := range cases {
		if got := normalizeServerAddr(tc.in); got != tc.want {
			t.Errorf("normalizeServerAddr(%q) = %q, want %q", tc.in, got, tc.want)
		}
	}
}

// startMiniTCPTNFS starts a minimal in-process read-only TCP TNFS server over root.
func startMiniTCPTNFS(t *testing.T, root string) (string, func()) {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	go func() {
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			go serveMiniTNFS(c, root)
		}
	}()
	return ln.Addr().String(), func() { _ = ln.Close() }
}

func serveMiniTNFS(conn net.Conn, root string) {
	defer conn.Close()
	r := bufio.NewReader(conn)
	files := map[byte]*os.File{}
	dirs := map[byte][]os.DirEntry{}
	dirIdx := map[byte]int{}
	defer func() {
		for _, f := range files {
			_ = f.Close()
		}
	}()

	var nextH byte = 1
	var hdr [4]byte
	for {
		if _, err := io.ReadFull(r, hdr[:]); err != nil {
			return
		}
		sid := binary.LittleEndian.Uint16(hdr[0:2])
		seq := hdr[2]
		cmd := hdr[3]
		var payload []byte

		switch cmd {
		case tnfsCmdMount:
			var ver [2]byte
			_, _ = io.ReadFull(r, ver[:])
			_, _ = r.ReadBytes(0)
			_, _ = r.ReadBytes(0)
			_, _ = r.ReadBytes(0)
			sid = 0x1234
			payload = []byte{tnfsOK, 0x02, 0x01, 100, 0}

		case tnfsCmdUmount:
			payload = []byte{tnfsOK}

		case tnfsCmdStat:
			p, _ := r.ReadBytes(0)
			rel := string(p[:len(p)-1])
			full := filepath.Join(root, filepath.FromSlash(cleanTNFSPath(rel)))
			fi, err := os.Stat(full)
			if err != nil {
				payload = []byte{tnfsErrNoEnt}
			} else {
				var out [25]byte
				out[0] = tnfsOK
				mode := uint16(0100444)
				if fi.IsDir() {
					mode = 0040555
				}
				binary.LittleEndian.PutUint16(out[1:3], mode)
				binary.LittleEndian.PutUint32(out[7:11], uint32(fi.Size()))
				mt := uint32(fi.ModTime().Unix())
				binary.LittleEndian.PutUint32(out[11:15], mt)
				binary.LittleEndian.PutUint32(out[15:19], mt)
				binary.LittleEndian.PutUint32(out[19:23], mt)
				payload = out[:]
			}

		case tnfsCmdOpenDirX:
			var h4 [4]byte
			_, _ = io.ReadFull(r, h4[:])
			_, _ = r.ReadBytes(0)
			p, _ := r.ReadBytes(0)
			rel := string(p[:len(p)-1])
			full := filepath.Join(root, filepath.FromSlash(cleanTNFSPath(rel)))
			entries, err := os.ReadDir(full)
			if err != nil {
				payload = []byte{tnfsErrNoEnt}
			} else {
				dh := nextH
				nextH++
				dirs[dh] = entries
				dirIdx[dh] = 0
				var out [4]byte
				out[0] = tnfsOK
				out[1] = dh
				binary.LittleEndian.PutUint16(out[2:4], uint16(len(entries)))
				payload = out[:]
			}

		case tnfsCmdReadDirX:
			var req [2]byte
			_, _ = io.ReadFull(r, req[:])
			dh := req[0]
			entries := dirs[dh]
			pos := dirIdx[dh]
			if pos >= len(entries) {
				payload = []byte{tnfsErrEOF}
			} else {
				var b bytes.Buffer
				for i := pos; i < len(entries); i++ {
					fi, _ := entries[i].Info()
					var flags byte
					if entries[i].IsDir() {
						flags = tnfsDirEntryDir
					}
					var rec [13]byte
					rec[0] = flags
					binary.LittleEndian.PutUint32(rec[1:5], uint32(fi.Size()))
					mt := uint32(fi.ModTime().Unix())
					binary.LittleEndian.PutUint32(rec[5:9], mt)
					binary.LittleEndian.PutUint32(rec[9:13], mt)
					b.Write(rec[:])
					b.WriteString(entries[i].Name())
					b.WriteByte(0)
				}
				dirIdx[dh] = len(entries)
				out := make([]byte, 5+b.Len())
				out[0] = tnfsOK
				out[1] = byte(len(entries) - pos)
				out[2] = tnfsDirStatEOF
				binary.LittleEndian.PutUint16(out[3:5], uint16(pos))
				copy(out[5:], b.Bytes())
				payload = out
			}

		case tnfsCmdCloseDir:
			dh, _ := r.ReadByte()
			delete(dirs, dh)
			payload = []byte{tnfsOK}

		case tnfsCmdOpen:
			var h4 [4]byte
			_, _ = io.ReadFull(r, h4[:])
			p, _ := r.ReadBytes(0)
			rel := string(p[:len(p)-1])
			full := filepath.Join(root, filepath.FromSlash(cleanTNFSPath(rel)))
			f, err := os.Open(full)
			if err != nil {
				payload = []byte{tnfsErrNoEnt}
			} else {
				fd := nextH
				nextH++
				files[fd] = f
				payload = []byte{tnfsOK, fd}
			}

		case tnfsCmdLseek:
			var req [6]byte
			_, _ = io.ReadFull(r, req[:])
			f := files[req[0]]
			off := int32(binary.LittleEndian.Uint32(req[2:6]))
			np, _ := f.Seek(int64(off), int(req[1]))
			var out [5]byte
			out[0] = tnfsOK
			binary.LittleEndian.PutUint32(out[1:5], uint32(np))
			payload = out[:]

		case tnfsCmdRead:
			var req [3]byte
			_, _ = io.ReadFull(r, req[:])
			f := files[req[0]]
			want := int(binary.LittleEndian.Uint16(req[1:3]))
			buf := make([]byte, 3+want)
			n, err := f.Read(buf[3:])
			if n == 0 && err == io.EOF {
				payload = []byte{tnfsErrEOF}
			} else {
				buf[0] = tnfsOK
				binary.LittleEndian.PutUint16(buf[1:3], uint16(n))
				payload = buf[:3+n]
			}

		case tnfsCmdClose:
			fd, _ := r.ReadByte()
			if f := files[fd]; f != nil {
				_ = f.Close()
				delete(files, fd)
			}
			payload = []byte{tnfsOK}
		}

		resp := make([]byte, 4+len(payload))
		binary.LittleEndian.PutUint16(resp[0:2], sid)
		resp[2] = seq
		resp[3] = cmd
		copy(resp[4:], payload)
		_, _ = conn.Write(resp)
	}
}

func TestTNFSFuseMountReadOnly(t *testing.T) {
	if _, err := os.Stat("/dev/fuse"); err != nil {
		t.Skipf("skipping FUSE mount test: /dev/fuse unavailable (%v)", err)
	}

	srcDir := t.TempDir()
	subDir := filepath.Join(srcDir, "coco")
	if err := os.Mkdir(subDir, 0755); err != nil {
		t.Fatal(err)
	}
	wantBytes := bytes.Repeat([]byte("OS9-FUSE-TNFS-"), 512) // > 4KB to test multi-chunk reads
	if err := os.WriteFile(filepath.Join(subDir, "disk.dsk"), wantBytes, 0644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(srcDir, "readme.txt"), []byte("hello fuse\n"), 0644); err != nil {
		t.Fatal(err)
	}

	addr, stopSrv := startMiniTCPTNFS(t, srcDir)
	defer stopSrv()

	client, err := dialTNFS(addr)
	if err != nil {
		t.Fatalf("dialTNFS: %v", err)
	}
	defer client.Close()

	mntDir := t.TempDir()
	srv, err := mountTNFS(mntDir, client, false)
	if err != nil {
		t.Skipf("FUSE mount unavailable in this environment: %v", err)
	}
	defer func() {
		_ = srv.Unmount()
	}()

	entries, err := os.ReadDir(mntDir)
	if err != nil {
		t.Fatalf("ReadDir(mnt): %v", err)
	}
	if len(entries) != 2 {
		t.Fatalf("got %d entries, want 2", len(entries))
	}

	gotReadme, err := os.ReadFile(filepath.Join(mntDir, "readme.txt"))
	if err != nil {
		t.Fatalf("ReadFile(readme.txt): %v", err)
	}
	if string(gotReadme) != "hello fuse\n" {
		t.Fatalf("readme.txt = %q, want %q", gotReadme, "hello fuse\n")
	}

	gotDisk, err := os.ReadFile(filepath.Join(mntDir, "coco", "disk.dsk"))
	if err != nil {
		t.Fatalf("ReadFile(coco/disk.dsk): %v", err)
	}
	if !bytes.Equal(gotDisk, wantBytes) {
		t.Fatalf("disk.dsk mismatch: got %d bytes, want %d bytes", len(gotDisk), len(wantBytes))
	}

	// Verify read-only: writes, mkdir, and unlink must fail.
	if err := os.WriteFile(filepath.Join(mntDir, "new.txt"), []byte("x"), 0644); err == nil {
		t.Fatal("expected WriteFile on read-only FUSE mount to fail")
	}
	if err := os.Mkdir(filepath.Join(mntDir, "newdir"), 0755); err == nil {
		t.Fatal("expected Mkdir on read-only FUSE mount to fail")
	}
	if err := os.Remove(filepath.Join(mntDir, "readme.txt")); err == nil {
		t.Fatal("expected Remove on read-only FUSE mount to fail")
	}
}
