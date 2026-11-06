package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

var DISK_ROOT = flag.String("disk_root", "", "folder the /disks page starts in (default: your home folder)")

// rpcMu guards tetherHandles and the slot state, which the /disks page changes while RPCs run.
var rpcMu sync.Mutex

type diskSlots struct {
	Remote  [4]string `json:"remote"`  // host file for /pc/fN; "" means the -pc folder's fN
	OnBoard [4]string `json:"onboard"` // host file this page last copied to /fd/fN
}

var disks diskSlots
var pendingOnBoard [4]string
var remoteSlotHandles = map[int]int{}
var onBoardSources []string
var onBoardReadFiles [4]string
var applyBusy bool
var applyStatus string

// What the one-time read found in /fd/fN. MainFile is the disk's only or largest DECB file.
type onBoardDrive struct {
	Read     bool   `json:"read"`
	Size     int64  `json:"size"`
	MainFile string `json:"mainFile"`
}

var onBoardDrives [4]onBoardDrive
var flashReadStarted bool

// The page needs the CoCo running for anything on-board, because the board's shell waits on it.
var pageOpened bool
var lastPagePoll time.Time
var cocoKnown, cocoOn bool

var eTransitions = regexp.MustCompile(`e_trans=(\d+)`)

// cocoRunning reads the board's status: a CoCo that is off has no E clock.
func cocoRunning(ch chan []byte) (bool, bool) {
	resp, err := PicoRpcCallReq(ch, RpcRequest{Method: "get-status"}, 2*time.Second)
	if err != nil || resp.Status != 0 {
		return false, false
	}
	m := eTransitions.FindStringSubmatch(resp.Message)
	if m == nil {
		return false, false
	}
	n, _ := strconv.Atoi(m[1])
	return n > 0, true
}

// watchCoco checks the CoCo while the page is open, and starts the one-time read once it is on.
func watchCoco() {
	for range time.Tick(2 * time.Second) {
		rpcMu.Lock()
		watching := time.Since(lastPagePoll) < 10*time.Second && !applyBusy
		rpcMu.Unlock()
		ch := GetChannelToPico()
		if !watching || ch == nil {
			continue
		}
		on, ok := cocoRunning(ch)
		rpcMu.Lock()
		cocoKnown, cocoOn = ok, on
		start := ok && on && pageOpened && !flashReadStarted && !applyBusy
		if start {
			flashReadStarted = true
			applyBusy = true
		}
		rpcMu.Unlock()
		if start {
			go readOnBoard()
		}
	}
}

// The board's shell copies staged images from /pc/.disk-copy/N, and sends drives for reading to /pc/.disk-read/N.
const diskCopyPrefix = "/.disk-copy/"
const diskReadPrefix = "/.disk-read/"

func diskStart() string {
	if *DISK_ROOT != "" {
		return *DISK_ROOT
	}
	if home, err := os.UserHomeDir(); err == nil {
		return home
	}
	return "/"
}

func diskSlotsFile() string {
	dir, err := os.UserConfigDir()
	if err != nil {
		return ""
	}
	return filepath.Join(dir, "centipede-tether", "disk-slots.json")
}

func loadDiskSlots() {
	if b, err := os.ReadFile(diskSlotsFile()); err == nil {
		json.Unmarshal(b, &disks)
	}
}

// saveDiskSlots needs rpcMu held.
func saveDiskSlots() {
	p := diskSlotsFile()
	if p == "" {
		return
	}
	os.MkdirAll(filepath.Dir(p), 0755)
	if b, err := json.MarshalIndent(&disks, "", "  "); err == nil {
		os.WriteFile(p, b, 0644)
	}
}

func remoteSlotOf(clean string) (int, bool) {
	if len(clean) == 3 && clean[:2] == "/f" && clean[2] >= '0' && clean[2] <= '3' {
		return int(clean[2] - '0'), true
	}
	return 0, false
}

// pcRealPath maps a /pc path from the board to a host file, honoring slot assignments.
// It needs rpcMu held.
func pcRealPath(p string) string {
	clean := filepath.Clean("/" + p)
	if slot, ok := remoteSlotOf(clean); ok && disks.Remote[slot] != "" {
		return disks.Remote[slot]
	}
	if strings.HasPrefix(clean, diskCopyPrefix) {
		if n, err := strconv.Atoi(clean[len(diskCopyPrefix):]); err == nil && n >= 0 && n < len(onBoardSources) {
			return onBoardSources[n]
		}
	}
	if strings.HasPrefix(clean, diskReadPrefix) {
		if n, err := strconv.Atoi(clean[len(diskReadPrefix):]); err == nil && n >= 0 && n < 4 && onBoardReadFiles[n] != "" {
			return onBoardReadFiles[n]
		}
	}
	return filepath.Join(*PC_DIR, clean)
}

func absDiskPath(p string) string {
	if p == "" {
		return diskStart()
	}
	if !filepath.IsAbs(p) {
		return filepath.Join(diskStart(), p)
	}
	return filepath.Clean(p)
}

// The tether listens on every interface by default, so only this computer may browse its files.
func isLocalRequest(r *http.Request) bool {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return false
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func localOnly(h http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !isLocalRequest(r) {
			http.Error(w, "The disk page only works from the computer running the tether.", http.StatusForbidden)
			return
		}
		h(w, r)
	}
}

func openForSwap(path string) (*os.File, error) {
	f, err := os.OpenFile(path, os.O_RDWR, 0)
	if err != nil {
		f, err = os.Open(path)
	}
	return f, err
}

// assignRemote points /pc/fN at path, or back at the -pc folder's fN when path is "".
// A drive the board already has open switches to the new file at its next access.
func assignRemote(slot int, path string) error {
	rpcMu.Lock()
	defer rpcMu.Unlock()
	target := path
	if target == "" {
		target = filepath.Join(*PC_DIR, fmt.Sprintf("f%d", slot))
	}
	for h, s := range remoteSlotHandles {
		if s != slot {
			continue
		}
		f, err := openForSwap(target)
		if err != nil {
			return err
		}
		if old, ok := tetherHandles[h]; ok {
			old.Close()
		}
		tetherHandles[h] = f
	}
	disks.Remote[slot] = path
	saveDiskSlots()
	return nil
}

func picoRestart(ch chan []byte, bootMode uint32) {
	payload := []byte{byte(bootMode >> 24), byte(bootMode >> 16), byte(bootMode >> 8), byte(bootMode)}
	PicoRpcCall(ch, "restart", payload, 3*time.Second) // The reboot can cut off the reply.
}

func waitForPico(ch chan []byte, limit time.Duration) bool {
	for start := time.Now(); time.Since(start) < limit; {
		if PicoRpcPing(ch, 42) == nil {
			return true
		}
		time.Sleep(time.Second)
	}
	return false
}

func setApplyStatus(s string) {
	rpcMu.Lock()
	applyStatus = s
	rpcMu.Unlock()
	log.Printf("disks: %s", s)
}

var ansiEscape = regexp.MustCompile(`\x1b\[[0-9;]*[A-Za-z]`)
var fdListLine = regexp.MustCompile(`^\s*f([0-3])\s+(\d+)\s*$`)

func shellLines(data []byte) []string {
	var out []string
	for _, line := range strings.Split(ansiEscape.ReplaceAllString(string(data), ""), "\n") {
		if line = strings.TrimSpace(line); line != "" {
			out = append(out, line)
		}
	}
	return out
}

// listOnBoard needs the board in its shell. It records each /fd drive's size and main file.
func listOnBoard(ch chan []byte) error {
	resp, err := PicoRpcCall(ch, "inject", []byte("ls -l /fd"), time.Minute)
	if err != nil {
		return err
	}
	var drives [4]onBoardDrive
	for i := range drives {
		drives[i].Read = true
	}
	for _, line := range shellLines(resp.Data) {
		if m := fdListLine.FindStringSubmatch(line); m != nil {
			n := int(m[1][0] - '0')
			drives[n].Size, _ = strconv.ParseInt(m[2], 10, 64)
		}
	}
	tmp, err := os.MkdirTemp("", "disk-read-*")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmp)
	for i := range drives {
		if drives[i].Size == 0 {
			continue
		}
		local := filepath.Join(tmp, fmt.Sprintf("f%d", i))
		rpcMu.Lock()
		onBoardReadFiles[i] = local
		rpcMu.Unlock()
		r, err := PicoRpcCall(ch, "inject", []byte(fmt.Sprintf("cp /fd/f%d /pc%s%d", i, diskReadPrefix, i)), 5*time.Minute)
		rpcMu.Lock()
		onBoardReadFiles[i] = ""
		rpcMu.Unlock()
		if err == nil && r.Status == 0 {
			if img, err := os.ReadFile(local); err == nil {
				drives[i].MainFile = decbMainFile(img)
			}
		}
	}
	rpcMu.Lock()
	onBoardDrives = drives
	rpcMu.Unlock()
	return nil
}

// decbMainFile names a DECB disk image's only file, or its largest one.
func decbMainFile(img []byte) string {
	const fatAt = (17*18 + 1) * 256
	const dirAt = (17*18 + 2) * 256
	if len(img) < dirAt+9*256 {
		return ""
	}
	fat := img[fatAt : fatAt+68]
	best, bestSize := "", -1
	for off := dirAt; off < dirAt+9*256; off += 32 {
		e := img[off : off+32]
		if e[0] == 0xFF {
			break
		}
		if e[0] == 0 {
			continue
		}
		size, g := 0, int(e[13])
		for steps := 0; g < len(fat) && steps < len(fat); steps++ {
			next := int(fat[g])
			if next >= 0xC0 {
				sectors := next & 0x1F
				if sectors > 0 {
					size += (sectors-1)*256 + int(e[14])<<8 + int(e[15])
				}
				break
			}
			size += 9 * 256
			g = next
		}
		name := strings.TrimRight(string(e[0:8]), " ")
		if ext := strings.TrimRight(string(e[8:11]), " "); ext != "" {
			name += "." + ext
		}
		if size > bestSize {
			best, bestSize = name, size
		}
	}
	return best
}

// readOnBoard lists the /fd drives through the board's shell, then returns to the menu.
func readOnBoard() {
	defer func() {
		rpcMu.Lock()
		applyBusy = false
		rpcMu.Unlock()
	}()
	ch := GetChannelToPico()
	setApplyStatus("Reading the on-board drives; the CoCo is restarting...")
	picoRestart(ch, 27)
	time.Sleep(3 * time.Second)
	if !waitForPico(ch, 60*time.Second) {
		setApplyStatus("The board did not come back after restarting, so the on-board drives were not read.")
		return
	}
	err := listOnBoard(ch)
	picoRestart(ch, 1)
	if err != nil {
		setApplyStatus(fmt.Sprintf("Could not read the on-board drives: %v. The board was sent back to its menu.", err))
		return
	}
	setApplyStatus("Read the on-board drives. The board is back at its menu.")
}

// applyOnBoard copies every staged image into /fd through the board's shell, then returns to the menu.
func applyOnBoard() {
	rpcMu.Lock()
	var cmds []string
	var slots []int
	var srcs []string
	for slot, p := range pendingOnBoard {
		if p == "" {
			continue
		}
		cmds = append(cmds, fmt.Sprintf("cp /pc%s%d /fd/f%d", diskCopyPrefix, len(srcs), slot))
		srcs = append(srcs, p)
		slots = append(slots, slot)
	}
	onBoardSources = srcs
	rpcMu.Unlock()
	defer func() {
		rpcMu.Lock()
		applyBusy = false
		rpcMu.Unlock()
	}()

	ch := GetChannelToPico()
	if ch == nil || len(cmds) == 0 {
		setApplyStatus("Nothing copied: the board is not connected, or nothing is staged.")
		return
	}
	setApplyStatus("Restarting the board into its shell...")
	picoRestart(ch, 27)
	time.Sleep(3 * time.Second)
	if !waitForPico(ch, 60*time.Second) {
		setApplyStatus("The board did not come back after restarting. Nothing was copied.")
		return
	}
	setApplyStatus(fmt.Sprintf("Copying %d disk image(s) to flash...", len(cmds)))
	resp, err := PicoRpcCall(ch, "inject", []byte(strings.Join(cmds, " ; ")), 5*time.Minute)
	ok := err == nil && resp.Status == 0

	rpcMu.Lock()
	if ok {
		for i, slot := range slots {
			disks.OnBoard[slot] = srcs[i]
			pendingOnBoard[slot] = ""
			d := onBoardDrive{Read: true}
			if info, err := os.Stat(srcs[i]); err == nil {
				d.Size = info.Size()
			}
			onBoardDrives[slot] = d
		}
		saveDiskSlots()
	}
	onBoardSources = nil
	rpcMu.Unlock()

	picoRestart(ch, 1)
	switch {
	case err != nil:
		setApplyStatus(fmt.Sprintf("Copy failed: %v. The board was sent back to its menu.", err))
	case resp.Status != 0:
		setApplyStatus(fmt.Sprintf("Copy failed: %s. The board was sent back to its menu.", strings.TrimSpace(string(resp.Data))))
	default:
		setApplyStatus("Copied. The board is back at its menu; choose On-Board Floppy Disks to use them.")
	}
}

type diskEntry struct {
	Name string `json:"name"`
	Dir  bool   `json:"dir"`
	Size int64  `json:"size"`
}

func listDiskDir(dir string) (string, []diskEntry, error) {
	clean := absDiskPath(dir)
	items, err := os.ReadDir(clean)
	if err != nil {
		return clean, nil, err
	}
	var out []diskEntry
	for _, it := range items {
		if strings.HasPrefix(it.Name(), ".") {
			continue
		}
		info, err := it.Info()
		if err != nil {
			continue
		}
		out = append(out, diskEntry{Name: it.Name(), Dir: info.IsDir(), Size: info.Size()})
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].Dir != out[j].Dir {
			return out[i].Dir
		}
		return strings.ToLower(out[i].Name) < strings.ToLower(out[j].Name)
	})
	return clean, out, nil
}

func shownPath(p string) string {
	if home, err := os.UserHomeDir(); err == nil && p != "" && strings.HasPrefix(p, home+"/") {
		return "~" + p[len(home):]
	}
	return p
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(v)
}

func serveDiskState(w http.ResponseWriter, r *http.Request) {
	rpcMu.Lock()
	defer rpcMu.Unlock()
	lastPagePoll = time.Now()
	type slot struct {
		Shown   string        `json:"shown"`
		Pending string        `json:"pending,omitempty"`
		Drive   *onBoardDrive `json:"drive,omitempty"`
	}
	var remote, onboard [4]slot
	for i := 0; i < 4; i++ {
		remote[i].Shown = shownPath(disks.Remote[i])
		if remote[i].Shown == "" {
			remote[i].Shown = fmt.Sprintf("f%d in the -pc folder", i)
		}
		onboard[i].Shown = shownPath(disks.OnBoard[i])
		onboard[i].Pending = shownPath(pendingOnBoard[i])
		if onBoardDrives[i].Read {
			d := onBoardDrives[i]
			onboard[i].Drive = &d
		}
	}
	writeJSON(w, map[string]any{
		"start": diskStart(), "remote": remote, "onboard": onboard,
		"busy": applyBusy, "status": applyStatus,
		"connected": GetChannelToPico() != nil,
		"cocoKnown": cocoKnown, "cocoOn": cocoOn,
	})
}

func serveDiskList(w http.ResponseWriter, r *http.Request) {
	dir, entries, err := listDiskDir(r.URL.Query().Get("dir"))
	if err != nil {
		http.Error(w, err.Error(), http.StatusNotFound)
		return
	}
	writeJSON(w, map[string]any{"dir": dir, "entries": entries})
}

func serveDiskAssign(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}
	var req struct {
		Kind string `json:"kind"`
		Slot int    `json:"slot"`
		Path string `json:"path"` // absolute; "" clears the slot
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Slot < 0 || req.Slot > 3 {
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}
	host := ""
	if req.Path != "" {
		host = absDiskPath(req.Path)
		if info, err := os.Stat(host); err != nil || info.IsDir() {
			http.Error(w, "not a file: "+req.Path, http.StatusBadRequest)
			return
		}
	}
	switch req.Kind {
	case "remote":
		if err := assignRemote(req.Slot, host); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
	case "onboard":
		rpcMu.Lock()
		busy, on := applyBusy, cocoOn
		if !busy && on {
			pendingOnBoard[req.Slot] = host
		}
		rpcMu.Unlock()
		if busy {
			http.Error(w, "a copy to flash is in progress", http.StatusConflict)
			return
		}
		if !on {
			http.Error(w, "The CoCo is off. Turn it on to change the on-board drives.", http.StatusConflict)
			return
		}
	default:
		http.Error(w, "kind must be remote or onboard", http.StatusBadRequest)
		return
	}
	serveDiskState(w, r)
}

func serveDiskApply(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}
	rpcMu.Lock()
	start := !applyBusy && cocoOn
	if start {
		applyBusy = true
		applyStatus = "Starting..."
	}
	rpcMu.Unlock()
	if start {
		go applyOnBoard()
	}
	serveDiskState(w, r)
}

// pageLoaded arms the one-time read; an open tab polling does not.
func pageLoaded() {
	rpcMu.Lock()
	pageOpened = true
	lastPagePoll = time.Now()
	rpcMu.Unlock()
}

func registerDiskHandlers() {
	loadDiskSlots()
	go watchCoco()
	http.HandleFunc("/disks", localOnly(func(w http.ResponseWriter, r *http.Request) {
		pageLoaded()
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		fmt.Fprint(w, DisksPage)
	}))
	http.HandleFunc("/disks/state", localOnly(serveDiskState))
	http.HandleFunc("/disks/list", localOnly(serveDiskList))
	http.HandleFunc("/disks/assign", localOnly(serveDiskAssign))
	http.HandleFunc("/disks/apply", localOnly(serveDiskApply))
}
