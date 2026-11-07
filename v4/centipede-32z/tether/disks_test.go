package main

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func withDiskTestDirs(t *testing.T) (pcDir, root string) {
	t.Helper()
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	pcDir, root = t.TempDir(), t.TempDir()
	savedPc, savedRoot := *PC_DIR, *DISK_ROOT
	savedDisks, savedPending := disks, pendingOnBoard
	*PC_DIR, *DISK_ROOT = pcDir, root
	disks, pendingOnBoard = diskSlots{}, [4]string{}
	t.Cleanup(func() {
		*PC_DIR, *DISK_ROOT = savedPc, savedRoot
		disks, pendingOnBoard = savedDisks, savedPending
		onBoardSources, onBoardReadFiles = nil, [4]string{}
	})
	return pcDir, root
}

func TestPcRealPath(t *testing.T) {
	pcDir, root := withDiskTestDirs(t)
	if got := pcRealPath("/f1"); got != filepath.Join(pcDir, "f1") {
		t.Errorf("unassigned: %q", got)
	}
	disks.Remote[1] = filepath.Join(root, "game.dsk")
	if got := pcRealPath("f1"); got != disks.Remote[1] {
		t.Errorf("assigned: %q", got)
	}
	if got := pcRealPath("/f10"); got != filepath.Join(pcDir, "f10") {
		t.Errorf("not a drive: %q", got)
	}
	onBoardSources = []string{"/x/a.dsk"}
	if got := pcRealPath(diskCopyPrefix + "0"); got != "/x/a.dsk" {
		t.Errorf("staged copy: %q", got)
	}
	onBoardReadFiles[2] = "/tmp/read2"
	if got := pcRealPath(diskReadPrefix + "2"); got != "/tmp/read2" {
		t.Errorf("drive being read: %q", got)
	}
	if got := pcRealPath(diskCopyPrefix + "1"); got != filepath.Join(pcDir, diskCopyPrefix+"1") {
		t.Errorf("no such staged copy: %q", got)
	}
}

func TestDiskPagesAreLocalOnly(t *testing.T) {
	withDiskTestDirs(t)
	h := localOnly(serveDiskState)
	for addr, want := range map[string]int{"127.0.0.1:5000": 200, "[::1]:5000": 200, "192.168.1.20:5000": http.StatusForbidden} {
		req := httptest.NewRequest(http.MethodGet, "/disks/state", nil)
		req.RemoteAddr = addr
		w := httptest.NewRecorder()
		h(w, req)
		if w.Code != want {
			t.Errorf("%s: got %d, want %d", addr, w.Code, want)
		}
	}
}

func TestAssignRemoteSwapsOpenDrive(t *testing.T) {
	pcDir, root := withDiskTestDirs(t)
	os.WriteFile(filepath.Join(pcDir, "f2"), []byte("old"), 0644)
	os.WriteFile(filepath.Join(root, "new.dsk"), []byte("new"), 0644)

	f, err := os.Open(filepath.Join(pcDir, "f2"))
	if err != nil {
		t.Fatal(err)
	}
	const h = 9999
	tetherHandles[h] = f
	remoteSlotHandles[h] = 2
	defer func() { tetherHandles[h].Close(); delete(tetherHandles, h); delete(remoteSlotHandles, h) }()

	if err := assignRemote(2, filepath.Join(root, "new.dsk")); err != nil {
		t.Fatal(err)
	}
	b, _ := io.ReadAll(tetherHandles[h])
	if string(b) != "new" {
		t.Errorf("open drive reads %q", b)
	}

	if err := assignRemote(2, ""); err != nil {
		t.Fatal(err)
	}
	b, _ = io.ReadAll(tetherHandles[h])
	if string(b) != "old" || disks.Remote[2] != "" {
		t.Errorf("reset: reads %q, slot %q", b, disks.Remote[2])
	}
}

func TestAssignmentsPersist(t *testing.T) {
	_, root := withDiskTestDirs(t)
	os.WriteFile(filepath.Join(root, "a.dsk"), nil, 0644)
	if err := assignRemote(0, filepath.Join(root, "a.dsk")); err != nil {
		t.Fatal(err)
	}
	disks = diskSlots{}
	loadDiskSlots()
	if disks.Remote[0] != filepath.Join(root, "a.dsk") {
		t.Errorf("reloaded %q", disks.Remote[0])
	}
}

func TestListDiskDir(t *testing.T) {
	_, root := withDiskTestDirs(t)
	os.Mkdir(filepath.Join(root, "Games"), 0755)
	os.WriteFile(filepath.Join(root, "b.dsk"), make([]byte, 161280), 0644)
	os.WriteFile(filepath.Join(root, "A.dsk"), nil, 0644)
	os.WriteFile(filepath.Join(root, ".hidden"), nil, 0644)

	dir, entries, err := listDiskDir("")
	if dir != root {
		t.Errorf("starts in %q", dir)
	}
	if err != nil {
		t.Fatal(err)
	}
	var names []string
	for _, e := range entries {
		names = append(names, e.Name)
	}
	if strings.Join(names, ",") != "Games,A.dsk,b.dsk" {
		t.Errorf("dir %q entries %v", dir, names)
	}
	if !entries[0].Dir || entries[2].Size != 161280 {
		t.Errorf("entries %+v", entries)
	}
}

func TestServeDiskAssign(t *testing.T) {
	_, root := withDiskTestDirs(t)
	cocoOn = true
	defer func() { cocoOn = false }()
	os.Mkdir(filepath.Join(root, "sub"), 0755)
	os.WriteFile(filepath.Join(root, "sub", "g.dsk"), nil, 0644)

	call := func(body string) *httptest.ResponseRecorder {
		w := httptest.NewRecorder()
		serveDiskAssign(w, httptest.NewRequest(http.MethodPost, "/disks/assign", bytes.NewBufferString(body)))
		return w
	}

	game := filepath.Join(root, "sub", "g.dsk")
	if w := call(`{"kind":"onboard","slot":3,"path":"` + game + `"}`); w.Code != 200 {
		t.Fatalf("stage: %d %s", w.Code, w.Body)
	}
	if pendingOnBoard[3] != filepath.Join(root, "sub", "g.dsk") {
		t.Errorf("staged %q", pendingOnBoard[3])
	}
	var st struct {
		Onboard [4]struct{ Pending string } `json:"onboard"`
	}
	json.Unmarshal(call(`{"kind":"onboard","slot":3,"path":"`+game+`"}`).Body.Bytes(), &st)
	if st.Onboard[3].Pending != game {
		t.Errorf("state shows %q", st.Onboard[3].Pending)
	}

	if w := call(`{"kind":"remote","slot":0,"path":"` + filepath.Join(root, "sub") + `"}`); w.Code != http.StatusBadRequest {
		t.Errorf("folder accepted: %d", w.Code)
	}
	if w := call(`{"kind":"remote","slot":4,"path":""}`); w.Code != http.StatusBadRequest {
		t.Errorf("slot 4 accepted: %d", w.Code)
	}
	if w := call(`{"kind":"onboard","slot":3,"path":""}`); w.Code != 200 || pendingOnBoard[3] != "" {
		t.Errorf("unstage: %d %q", w.Code, pendingOnBoard[3])
	}
}

func TestShellListing(t *testing.T) {
	lines := shellLines([]byte("\x1b[7m \x1b[0m\n  f0    161280\n\n  f1    0\r\n  f3    161280\n"))
	var sizes [4]int64
	for _, l := range lines {
		if m := fdListLine.FindStringSubmatch(l); m != nil {
			sizes[m[1][0]-'0'] = int64(len(m[2]))
		}
	}
	if len(lines) != 3 || sizes != [4]int64{6, 1, 0, 6} {
		t.Errorf("lines %q sizes %v", lines, sizes)
	}
}

func TestDecbMainFile(t *testing.T) {
	img := make([]byte, 161280)
	fat, dir := (17*18+1)*256, (17*18+2)*256
	for i := 0; i < 68; i++ {
		img[fat+i] = 0xFF
	}
	for i := 0; i < 9*256; i++ {
		img[dir+i] = 0xFF
	}
	entry := func(n int, name string, gran, lastBytes int) {
		e := img[dir+32*n:]
		copy(e, name)
		e[11], e[13], e[14], e[15] = 2, byte(gran), byte(lastBytes>>8), byte(lastBytes)
	}
	entry(0, "SMALL   BIN", 0, 100)
	img[fat+0] = 0xC1 // One sector.
	entry(1, "BIG     BIN", 1, 50)
	img[fat+1], img[fat+2] = 2, 0xC3 // Two granules, three sectors in the last.
	if got := decbMainFile(img); got != "BIG.BIN" {
		t.Errorf("got %q", got)
	}
	img[dir+32] = 0 // Deleted.
	if got := decbMainFile(img); got != "SMALL.BIN" {
		t.Errorf("one file: got %q", got)
	}
}

func TestOnBoardNeedsCocoOn(t *testing.T) {
	_, root := withDiskTestDirs(t)
	os.WriteFile(filepath.Join(root, "g.dsk"), nil, 0644)
	cocoOn = false
	w := httptest.NewRecorder()
	body := `{"kind":"onboard","slot":1,"path":"` + filepath.Join(root, "g.dsk") + `"}`
	serveDiskAssign(w, httptest.NewRequest(http.MethodPost, "/disks/assign", bytes.NewBufferString(body)))
	if w.Code != http.StatusConflict || pendingOnBoard[1] != "" {
		t.Errorf("staged with the CoCo off: %d %q", w.Code, pendingOnBoard[1])
	}
}

func TestStatusClock(t *testing.T) {
	for msg, want := range map[string]string{"halt=0 rst=1 e_trans=174 e_hi=1000": "174", "halt=0 rst=0 e_trans=0 e_hi=2000": "0"} {
		if m := eTransitions.FindStringSubmatch(msg); m == nil || m[1] != want {
			t.Errorf("%q: %v", msg, m)
		}
	}
}

func TestCocoOffClearsLastResult(t *testing.T) {
	savedStatus, savedKnown, savedOn, savedBusy := applyStatus, cocoKnown, cocoOn, applyBusy
	savedOpened, savedStarted := pageOpened, flashReadStarted
	t.Cleanup(func() {
		applyStatus, cocoKnown, cocoOn, applyBusy = savedStatus, savedKnown, savedOn, savedBusy
		pageOpened, flashReadStarted = savedOpened, savedStarted
	})
	pageOpened, flashReadStarted, applyBusy = true, true, false

	cocoKnown, cocoOn, applyStatus = true, true, "Read the on-board drives."
	noteCocoPower(true, true)
	if applyStatus == "" {
		t.Errorf("cleared while the CoCo stayed on")
	}
	noteCocoPower(false, false)
	if applyStatus == "" {
		t.Errorf("cleared on a failed reading")
	}
	cocoKnown, cocoOn = true, true
	noteCocoPower(true, false)
	if applyStatus != "" || cocoOn {
		t.Errorf("not cleared at power-off: %q on=%v", applyStatus, cocoOn)
	}

	cocoKnown, cocoOn, applyBusy, applyStatus = true, true, true, "Copying..."
	noteCocoPower(true, false)
	if applyStatus != "Copying..." {
		t.Errorf("cleared during a copy: %q", applyStatus)
	}
}
