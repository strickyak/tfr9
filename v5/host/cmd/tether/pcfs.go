package main

// The tether's virtual /pc filesystem: one directory (-pc, default /tmp/pc) served to the Pico.
// Rules (spec section 11.13, "threat model" 11.12):
//   * The root must exist and be a directory, checked on EVERY request; if not, every /pc operation
//     fails with "no /pc: ...".  We never create it (the operator makes it).
//   * Below the root, symlinks are INVISIBLE: each path component is checked with Lstat and a symlink
//     (on Windows also a junction/reparse point, which Go reports as ModeIrregular) is treated as
//     nonexistent, and is omitted from directory listings.  The root itself may be a symlink.
//   * Only regular files and directories are served (no FIFOs, sockets or devices).
//   * Path names are cleaned from "/" (no "..", no absolute escapes) and may not contain NUL or '\'.
//   * Opening uses O_NOFOLLOW where available and re-checks the opened file against the Lstat.
// Paths given on the command line (-disks, -just-upload, ...) are trusted and do not go through here.

import (
	"errors"
	"fmt"
	"io/fs"
	"log"
	"os"
	"path"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
)

// pcFinal says what is allowed of the LAST path component.
type pcFinal int

const (
	pcMustExist pcFinal = iota // stat, read-only open, dir_open, remove, rename source
	pcMayCreate                // open with O_CREATE, mkdir, rename destination: it may not exist yet
)

var (
	pcWarnMu  sync.Mutex
	pcWarned  bool
	pcMissing = errors.New("no /pc")
)

// pcRoot verifies the root and returns it (symlinks in the root path itself are resolved once).
func pcRoot() (string, error) {
	root := *PC_DIR
	fi, err := os.Stat(root) // follows a symlinked root on purpose (e.g. macOS /tmp -> /private/tmp)
	if err != nil || !fi.IsDir() {
		pcWarnMu.Lock()
		if !pcWarned {
			pcWarned = true
			log.Printf("/pc is disabled: directory %q does not exist (create it, or give -pc); all /pc operations will fail", root)
			fmt.Fprintf(os.Stderr, "tether: /pc is disabled: %s does not exist\n", root)
		}
		pcWarnMu.Unlock()
		return "", fmt.Errorf("%w: %s does not exist", pcMissing, root)
	}
	pcWarnMu.Lock()
	if pcWarned {
		pcWarned = false
		log.Printf("/pc is now available: %q", root)
	}
	pcWarnMu.Unlock()
	if r, err := filepath.EvalSymlinks(root); err == nil {
		root = r
	}
	return root, nil
}

func pcNotExist(rel string) error {
	return &fs.PathError{Op: "pc", Path: rel, Err: fs.ErrNotExist}
}

// pcResolve maps a Pico-side path to a real path, refusing symlinks and special files.
func pcResolve(rel string, final pcFinal) (string, error) {
	root, err := pcRoot()
	if err != nil {
		return "", err
	}
	if strings.ContainsAny(rel, "\x00\\") {
		return "", &fs.PathError{Op: "pc", Path: rel, Err: fs.ErrInvalid}
	}
	clean := path.Clean("/" + rel) // cannot climb above "/"
	if clean == "/" {
		return root, nil
	}
	comps := strings.Split(strings.TrimPrefix(clean, "/"), "/")
	cur := root
	for i, c := range comps {
		if runtime.GOOS == "windows" && pcWindowsBad(c) {
			return "", &fs.PathError{Op: "pc", Path: rel, Err: fs.ErrInvalid}
		}
		cur = filepath.Join(cur, c)
		last := i == len(comps)-1
		fi, err := os.Lstat(cur)
		if err != nil {
			if last && final == pcMayCreate && errors.Is(err, fs.ErrNotExist) {
				return cur, nil
			}
			return "", err
		}
		if fi.Mode()&(os.ModeSymlink|os.ModeIrregular) != 0 {
			return "", pcNotExist(rel) // symlinks are invisible
		}
		if !last && !fi.IsDir() {
			return "", &fs.PathError{Op: "pc", Path: rel, Err: errors.New("not a directory")}
		}
		if last && !fi.IsDir() && !fi.Mode().IsRegular() {
			return "", pcNotExist(rel) // FIFOs, sockets, devices are invisible too
		}
	}
	return cur, nil
}

func pcWindowsBad(c string) bool {
	if strings.ContainsRune(c, ':') {
		return true
	}
	base := strings.ToUpper(strings.SplitN(c, ".", 2)[0])
	switch base {
	case "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
		"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9":
		return true
	}
	return false
}

// pcOpenFile opens a resolved path without following a final symlink, then checks that what we got
// is the same regular file or directory that pcResolve looked at (narrows the check-then-open race).
func pcOpenFile(real string, flag int, perm os.FileMode) (*os.File, error) {
	before, _ := os.Lstat(real) // nil if we are creating it
	f, err := os.OpenFile(real, flag|pcNoFollowFlag, perm)
	if err != nil {
		return nil, err
	}
	after, err := f.Stat()
	if err != nil || !(after.IsDir() || after.Mode().IsRegular()) || (before != nil && !os.SameFile(before, after)) {
		f.Close()
		return nil, pcNotExist(real)
	}
	return f, nil
}

// pcVisible reports whether a directory entry may be shown to the Pico.
func pcVisible(e os.DirEntry) bool {
	t := e.Type()
	return t&(os.ModeSymlink|os.ModeIrregular|os.ModeDevice|os.ModeCharDevice|os.ModeNamedPipe|os.ModeSocket) == 0
}
