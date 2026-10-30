package main

import (
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"syscall"
	"testing"
)

// Spec section 11.13: the /pc filesystem refuses to see symlinks, serves only regular files and
// directories, stays inside the -pc root, and is disabled (all operations fail) if the root is missing.

func rpc(method, p string, opt func(*RpcRequest)) RpcResponse {
	req := RpcRequest{Method: method, Path: p}
	if opt != nil {
		opt(&req)
	}
	return handleRpcReq(req)
}

func mustWrite(t *testing.T, p, s string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(p), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(p, []byte(s), 0644); err != nil {
		t.Fatal(err)
	}
}

// hostile builds a root with legitimate files plus every kind of trap, and returns (root, outside).
func hostile(t *testing.T) (string, string) {
	t.Helper()
	base := t.TempDir()
	root := filepath.Join(base, "pc")
	outside := filepath.Join(base, "outside")
	mustWrite(t, filepath.Join(root, "file.txt"), "hello")
	mustWrite(t, filepath.Join(root, "dir", "inner.txt"), "inner")
	mustWrite(t, filepath.Join(outside, "secret.txt"), "TOP SECRET")
	for name, target := range map[string]string{
		"link-file":   "file.txt",
		"link-dir":    "dir",
		"escape-file": filepath.Join("..", "outside", "secret.txt"),
		"escape-dir":  filepath.Join("..", "outside"),
		"abs-escape":  filepath.Join(outside, "secret.txt"),
		"dangling":    "nowhere",
		"loop":        "loop",
		"dir/link-up": "..",
	} {
		if err := os.Symlink(target, filepath.Join(root, name)); err != nil {
			t.Skipf("cannot create symlinks here: %v", err)
		}
	}
	if runtime.GOOS != "windows" {
		if err := syscall.Mkfifo(filepath.Join(root, "fifo"), 0644); err != nil {
			t.Fatal(err)
		}
	}
	old := *PC_DIR
	*PC_DIR = root
	t.Cleanup(func() { *PC_DIR = old })
	return root, outside
}

func wantFail(t *testing.T, what string, r RpcResponse) {
	t.Helper()
	if r.Status == 0 {
		t.Errorf("%s: expected failure, got success (%+v)", what, r)
	}
}

func TestLegitimateAccessStillWorks(t *testing.T) {
	hostile(t)
	if r := rpc("stat", "/file.txt", nil); r.Status != 0 || r.Size != 5 {
		t.Errorf("stat file.txt: %+v", r)
	}
	if r := rpc("stat", "/dir/inner.txt", nil); r.Status != 0 {
		t.Errorf("stat dir/inner.txt: %+v", r)
	}
	r := rpc("open", "/file.txt", func(q *RpcRequest) { q.Flags = 1 })
	if r.Status != 0 {
		t.Fatalf("open: %+v", r)
	}
	rd := handleRpcReq(RpcRequest{Method: "read", Handle: r.Handle, Length: 100})
	if string(rd.Data) != "hello" {
		t.Errorf("read: %q", rd.Data)
	}
	handleRpcReq(RpcRequest{Method: "close", Handle: r.Handle})
	if r := rpc("mkdir", "/newdir", nil); r.Status != 0 {
		t.Errorf("mkdir: %+v", r)
	}
	c := rpc("open", "/newdir/new.txt", func(q *RpcRequest) { q.Flags = 2 | 0x0100 })
	if c.Status != 0 {
		t.Errorf("create: %+v", c)
	}
	handleRpcReq(RpcRequest{Method: "close", Handle: c.Handle})
}

func TestSymlinksAreInvisible(t *testing.T) {
	root, outside := hostile(t)
	for _, p := range []string{"/link-file", "/link-dir", "/link-dir/inner.txt", "/escape-file", "/escape-dir",
		"/escape-dir/secret.txt", "/abs-escape", "/dangling", "/loop", "/dir/link-up", "/dir/link-up/file.txt"} {
		wantFail(t, "stat "+p, rpc("stat", p, nil))
		wantFail(t, "open "+p, rpc("open", p, func(q *RpcRequest) { q.Flags = 1 }))
		wantFail(t, "dir_open "+p, rpc("dir_open", p, nil))
		wantFail(t, "remove "+p, rpc("remove", p, nil))
	}
	// the target outside must be untouched
	if b, _ := os.ReadFile(filepath.Join(outside, "secret.txt")); string(b) != "TOP SECRET" {
		t.Errorf("outside file changed: %q", b)
	}
	// every symlink must still exist (remove must not have deleted the link itself)
	for _, n := range []string{"link-file", "link-dir", "dangling", "loop"} {
		if _, err := os.Lstat(filepath.Join(root, n)); err != nil {
			t.Errorf("symlink %s was removed: %v", n, err)
		}
	}
}

func TestWritesThroughSymlinksFail(t *testing.T) {
	root, outside := hostile(t)
	wantFail(t, "create via link-dir", rpc("open", "/link-dir/x.txt", func(q *RpcRequest) { q.Flags = 2 | 0x0100 }))
	wantFail(t, "create via escape-dir", rpc("open", "/escape-dir/x.txt", func(q *RpcRequest) { q.Flags = 2 | 0x0100 }))
	wantFail(t, "create over symlink", rpc("open", "/escape-file", func(q *RpcRequest) { q.Flags = 2 | 0x0100 | 0x0400 }))
	wantFail(t, "mkdir via escape-dir", rpc("mkdir", "/escape-dir/new", nil))
	wantFail(t, "mkdir via link-dir", rpc("mkdir", "/link-dir/new", nil))
	wantFail(t, "rename into link-dir", rpc("rename", "/file.txt", func(q *RpcRequest) { q.Path2 = "/link-dir/file.txt" }))
	wantFail(t, "rename into escape-dir", rpc("rename", "/file.txt", func(q *RpcRequest) { q.Path2 = "/escape-dir/file.txt" }))
	wantFail(t, "rename a symlink", rpc("rename", "/link-file", func(q *RpcRequest) { q.Path2 = "/renamed" }))
	for _, p := range []string{filepath.Join(outside, "x.txt"), filepath.Join(outside, "new"), filepath.Join(outside, "file.txt"),
		filepath.Join(root, "dir", "x.txt"), filepath.Join(root, "dir", "new")} {
		if _, err := os.Lstat(p); err == nil {
			t.Errorf("%s was created through a symlink", p)
		}
	}
	if b, _ := os.ReadFile(filepath.Join(outside, "secret.txt")); string(b) != "TOP SECRET" {
		t.Errorf("outside file changed: %q", b)
	}
}

func TestDirectoryListingOmitsSymlinksAndSpecials(t *testing.T) {
	hostile(t)
	o := rpc("dir_open", "/", nil)
	if o.Status != 0 {
		t.Fatalf("dir_open /: %+v", o)
	}
	var names []string
	for {
		r := handleRpcReq(RpcRequest{Method: "dir_read", Handle: o.Handle})
		if r.Status != 0 {
			t.Fatalf("dir_read: %+v", r)
		}
		if len(r.Data) == 0 {
			break
		}
		names = append(names, string(r.Data))
	}
	handleRpcReq(RpcRequest{Method: "dir_close", Handle: o.Handle})
	sort.Strings(names)
	if got := strings.Join(names, ","); got != "dir,file.txt" {
		t.Errorf("visible entries = %q, want \"dir,file.txt\"", got)
	}
}

func TestDotDotAndAbsolutePathsStayInside(t *testing.T) {
	_, _ = hostile(t)
	// "../outside/secret.txt" is cleaned from "/" to "/outside/secret.txt" INSIDE the root, which doesn't exist.
	wantFail(t, "dotdot", rpc("stat", "../outside/secret.txt", nil))
	wantFail(t, "dotdot2", rpc("stat", "/dir/../../outside/secret.txt", nil))
	wantFail(t, "absolute", rpc("stat", "/etc/passwd", nil))
	if r := rpc("stat", "/dir/../file.txt", nil); r.Status != 0 {
		t.Errorf("benign .. inside the root should work: %+v", r)
	}
	wantFail(t, "backslash", rpc("stat", `dir\inner.txt`, nil))
	wantFail(t, "nul", rpc("stat", "file.txt\x00.png", nil))
}

func TestFifoIsRefused(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("no FIFOs")
	}
	hostile(t)
	wantFail(t, "stat fifo", rpc("stat", "/fifo", nil))
	wantFail(t, "open fifo", rpc("open", "/fifo", func(q *RpcRequest) { q.Flags = 1 })) // must not hang either
}

func TestMissingRootDisablesEverything(t *testing.T) {
	base := t.TempDir()
	old := *PC_DIR
	t.Cleanup(func() { *PC_DIR = old })
	*PC_DIR = filepath.Join(base, "pc") // does not exist
	for _, m := range []string{"stat", "open", "dir_open", "mkdir", "remove"} {
		r := rpc(m, "/", func(q *RpcRequest) { q.Flags = 0x0100 | 2 })
		if r.Status == 0 || !strings.Contains(r.Message, "no /pc") {
			t.Errorf("%s with missing root: %+v", m, r)
		}
	}
	r := rpc("rename", "/a", func(q *RpcRequest) { q.Path2 = "/b" })
	if r.Status == 0 {
		t.Errorf("rename with missing root: %+v", r)
	}
	if _, err := os.Stat(*PC_DIR); err == nil {
		t.Errorf("tether created the missing /pc directory; it must not")
	}
	// ...and creating it later makes /pc work, without restarting.
	if err := os.Mkdir(*PC_DIR, 0755); err != nil {
		t.Fatal(err)
	}
	if r := rpc("stat", "/", nil); r.Status != 0 || r.IsDir != 1 {
		t.Errorf("after mkdir, stat / = %+v", r)
	}
}

func TestSymlinkedRootItselfIsAllowed(t *testing.T) {
	base := t.TempDir()
	real := filepath.Join(base, "real")
	mustWrite(t, filepath.Join(real, "f.txt"), "x")
	link := filepath.Join(base, "link")
	if err := os.Symlink(real, link); err != nil {
		t.Skipf("no symlinks: %v", err)
	}
	old := *PC_DIR
	*PC_DIR = link
	t.Cleanup(func() { *PC_DIR = old })
	if r := rpc("stat", "/f.txt", nil); r.Status != 0 {
		t.Errorf("a symlinked root (e.g. macOS /tmp) must work: %+v", r)
	}
}

func TestReadLengthIsCapped(t *testing.T) {
	hostile(t)
	r := rpc("open", "/file.txt", func(q *RpcRequest) { q.Flags = 1 })
	rd := handleRpcReq(RpcRequest{Method: "read", Handle: r.Handle, Length: 1 << 30})
	if rd.Status != 0 || string(rd.Data) != "hello" {
		t.Errorf("huge read length: %+v", rd)
	}
	handleRpcReq(RpcRequest{Method: "close", Handle: r.Handle})
}
