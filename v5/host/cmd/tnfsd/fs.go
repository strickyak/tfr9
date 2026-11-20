package main

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

type fsEntry struct {
	Name  string
	IsDir bool
	Size  uint32
	MTime uint32
	CTime uint32
	ATime uint32
	Mode  uint16
}

// readOnlyFS exports a single host directory tree read-only while hiding
// dotfiles, symlinks/special files, and unreadable files.
type readOnlyFS struct {
	root string
}

func newReadOnlyFS(rootDir string) (*readOnlyFS, error) {
	if strings.TrimSpace(rootDir) == "" {
		return nil, errors.New("--fs directory must be non-empty")
	}
	fi, err := os.Stat(rootDir)
	if err != nil {
		return nil, fmt.Errorf("stat --fs %q: %w", rootDir, err)
	}
	if !fi.IsDir() {
		return nil, fmt.Errorf("--fs %q is not a directory", rootDir)
	}
	resolved, err := filepath.EvalSymlinks(rootDir)
	if err != nil {
		resolved = rootDir
	}
	return &readOnlyFS{root: resolved}, nil
}

// resolve maps a TNFS path relative to mountRoot onto a safe host path.
// Any symlink, irregular file, dot-prefixed component, or unreadable file is rejected.
func (r *readOnlyFS) resolve(mountRoot, rawPath string) (string, os.FileInfo, byte) {
	if strings.ContainsAny(rawPath, "\x00\\") {
		return "", nil, tnfsErrInvalidArg
	}
	baseClean := path.Clean("/" + mountRoot)
	relClean := path.Clean("/" + rawPath)
	combined := path.Clean(path.Join(baseClean, relClean))
	if combined == "/" {
		fi, err := os.Lstat(r.root)
		if err != nil || !fi.IsDir() {
			return "", nil, tnfsErrNoEnt
		}
		return r.root, fi, tnfsOK
	}

	comps := strings.Split(strings.TrimPrefix(combined, "/"), "/")
	cur := r.root
	var lastInfo os.FileInfo
	for i, c := range comps {
		if c == "" || strings.HasPrefix(c, ".") {
			return "", nil, tnfsErrNoEnt
		}
		cur = filepath.Join(cur, c)
		fi, err := os.Lstat(cur)
		if err != nil {
			return "", nil, tnfsErrNoEnt
		}
		if fi.Mode()&(os.ModeSymlink|os.ModeIrregular) != 0 {
			return "", nil, tnfsErrNoEnt
		}
		last := i == len(comps)-1
		if !last {
			if !fi.IsDir() {
				return "", nil, tnfsErrNotDir
			}
			continue
		}
		if !fi.IsDir() && !fi.Mode().IsRegular() {
			return "", nil, tnfsErrNoEnt
		}
		if fi.IsDir() {
			if !isReadableDir(cur) {
				return "", nil, tnfsErrNoEnt
			}
		} else {
			if !isReadableFile(cur, fi) {
				return "", nil, tnfsErrNoEnt
			}
		}
		lastInfo = fi
	}
	return cur, lastInfo, tnfsOK
}

func isReadableFile(realPath string, before os.FileInfo) bool {
	f, err := os.OpenFile(realPath, os.O_RDONLY|noFollowFlag, 0)
	if err != nil {
		return false
	}
	defer f.Close()
	after, err := f.Stat()
	if err != nil || !after.Mode().IsRegular() || !os.SameFile(before, after) {
		return false
	}
	return true
}

func isReadableDir(realPath string) bool {
	f, err := os.OpenFile(realPath, os.O_RDONLY|noFollowFlag, 0)
	if err != nil {
		return false
	}
	defer f.Close()
	fi, err := f.Stat()
	return err == nil && fi.IsDir()
}

func (r *readOnlyFS) openReadOnly(mountRoot, rawPath string) (*os.File, byte) {
	realPath, before, code := r.resolve(mountRoot, rawPath)
	if code != tnfsOK {
		return nil, code
	}
	if before.IsDir() {
		return nil, tnfsErrIsDir
	}
	f, err := os.OpenFile(realPath, os.O_RDONLY|noFollowFlag, 0)
	if err != nil {
		return nil, tnfsErrNoEnt
	}
	after, err := f.Stat()
	if err != nil || !after.Mode().IsRegular() || !os.SameFile(before, after) {
		_ = f.Close()
		return nil, tnfsErrNoEnt
	}
	return f, tnfsOK
}

func (r *readOnlyFS) statEntry(mountRoot, rawPath string) (fsEntry, byte) {
	_, fi, code := r.resolve(mountRoot, rawPath)
	if code != tnfsOK {
		return fsEntry{}, code
	}
	return fileInfoToEntry(filepath.Base(rawPath), fi), tnfsOK
}

func (r *readOnlyFS) listDir(mountRoot, rawPath string, diropt, sortopt byte, pattern string, maxResults uint16) ([]fsEntry, byte) {
	realPath, fi, code := r.resolve(mountRoot, rawPath)
	if code != tnfsOK {
		return nil, code
	}
	if !fi.IsDir() {
		return nil, tnfsErrNotDir
	}

	rawEntries, err := os.ReadDir(realPath)
	if err != nil {
		return nil, tnfsErrNoEnt
	}

	noFolders := (diropt & tnfsDirOptNoFolders) != 0
	noFoldersFirst := (diropt & tnfsDirOptNoFoldersFirst) != 0
	dirPattern := (diropt & tnfsDirOptDirPattern) != 0
	patLower := strings.ToLower(strings.TrimSpace(pattern))

	var entries []fsEntry
	for _, de := range rawEntries {
		name := de.Name()
		if name == "" || strings.HasPrefix(name, ".") {
			continue
		}
		if de.Type()&(os.ModeSymlink|os.ModeIrregular|os.ModeDevice|os.ModeCharDevice|os.ModeNamedPipe|os.ModeSocket) != 0 {
			continue
		}
		childPath := filepath.Join(realPath, name)
		cfi, err := os.Lstat(childPath)
		if err != nil {
			continue
		}
		if cfi.Mode()&(os.ModeSymlink|os.ModeIrregular) != 0 {
			continue
		}
		if !cfi.IsDir() && !cfi.Mode().IsRegular() {
			continue
		}
		if cfi.IsDir() {
			if noFolders || !isReadableDir(childPath) {
				continue
			}
			if dirPattern && patLower != "" {
				if matched, _ := path.Match(patLower, strings.ToLower(name)); !matched {
					continue
				}
			}
		} else {
			if !isReadableFile(childPath, cfi) {
				continue
			}
			if patLower != "" {
				if matched, _ := path.Match(patLower, strings.ToLower(name)); !matched {
					continue
				}
			}
		}
		entries = append(entries, fileInfoToEntry(name, cfi))
	}

	if (sortopt & tnfsDirSortNone) == 0 {
		caseSens := (sortopt & tnfsDirSortCase) != 0
		desc := (sortopt & tnfsDirSortDescending) != 0
		byMod := (sortopt & tnfsDirSortModified) != 0
		bySize := (sortopt & tnfsDirSortSize) != 0

		sort.Slice(entries, func(i, j int) bool {
			a, b := entries[i], entries[j]
			if !noFoldersFirst && a.IsDir != b.IsDir {
				return a.IsDir
			}
			var cmp int
			switch {
			case byMod:
				if a.MTime < b.MTime {
					cmp = -1
				} else if a.MTime > b.MTime {
					cmp = 1
				}
			case bySize:
				if a.Size < b.Size {
					cmp = -1
				} else if a.Size > b.Size {
					cmp = 1
				}
			}
			if cmp == 0 {
				na, nb := a.Name, b.Name
				if !caseSens {
					na = strings.ToLower(na)
					nb = strings.ToLower(nb)
				}
				cmp = strings.Compare(na, nb)
			}
			if desc {
				return cmp > 0
			}
			return cmp < 0
		})
	}

	if maxResults > 0 && len(entries) > int(maxResults) {
		entries = entries[:maxResults]
	}
	return entries, tnfsOK
}

func fileInfoToEntry(name string, fi fs.FileInfo) fsEntry {
	mt := uint32(fi.ModTime().Unix())
	var sz uint32
	mode := uint16(0100444)
	if fi.IsDir() {
		mode = 0040555
	} else if s := fi.Size(); s > 0 {
		if s > 0xFFFFFFFF {
			sz = 0xFFFFFFFF
		} else {
			sz = uint32(s)
		}
	}
	if mt == 0 {
		mt = uint32(time.Now().Unix())
	}
	return fsEntry{
		Name:  name,
		IsDir: fi.IsDir(),
		Size:  sz,
		MTime: mt,
		CTime: mt,
		ATime: mt,
		Mode:  mode,
	}
}
