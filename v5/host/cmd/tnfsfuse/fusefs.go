//go:build !windows

package main

import (
	"context"
	"path"
	"syscall"
	"time"

	"github.com/hanwen/go-fuse/v2/fs"
	"github.com/hanwen/go-fuse/v2/fuse"
)

type tnfsNode struct {
	fs.Inode
	client   *tnfsClient
	tnfsPath string
}

var (
	_ fs.NodeLookuper  = (*tnfsNode)(nil)
	_ fs.NodeGetattrer = (*tnfsNode)(nil)
	_ fs.NodeReaddirer = (*tnfsNode)(nil)
	_ fs.NodeOpener    = (*tnfsNode)(nil)
	_ fs.NodeStatfser  = (*tnfsNode)(nil)
)

func fillAttr(out *fuse.Attr, st tnfsStatInfo) {
	if st.IsDir {
		out.Mode = fuse.S_IFDIR | st.Mode
	} else {
		out.Mode = fuse.S_IFREG | st.Mode
	}
	out.Size = st.Size
	out.Atime = st.ATime
	out.Mtime = st.MTime
	out.Ctime = st.CTime
}

func (n *tnfsNode) Statfs(_ context.Context, out *fuse.StatfsOut) syscall.Errno {
	out.Bsize = 4096
	out.Frsize = 4096
	out.NameLen = 255
	out.Blocks = 262144
	return 0
}

func (n *tnfsNode) Getattr(_ context.Context, _ fs.FileHandle, out *fuse.AttrOut) syscall.Errno {
	st, err := n.client.Stat(n.tnfsPath)
	if err != nil {
		return errToErrno(err)
	}
	fillAttr(&out.Attr, st)
	return 0
}

func (n *tnfsNode) Lookup(ctx context.Context, name string, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) {
	childPath := path.Join(n.tnfsPath, name)
	st, err := n.client.Stat(childPath)
	if err != nil {
		return nil, errToErrno(err)
	}
	fillAttr(&out.Attr, st)
	child := &tnfsNode{
		client:   n.client,
		tnfsPath: childPath,
	}
	mode := uint32(fuse.S_IFREG)
	if st.IsDir {
		mode = fuse.S_IFDIR
	}
	return n.NewInode(ctx, child, fs.StableAttr{Mode: mode}), 0
}

func (n *tnfsNode) Readdir(_ context.Context) (fs.DirStream, syscall.Errno) {
	items, err := n.client.ListDir(n.tnfsPath)
	if err != nil {
		return nil, errToErrno(err)
	}
	entries := make([]fuse.DirEntry, 0, len(items))
	for _, it := range items {
		mode := uint32(fuse.S_IFREG)
		if it.IsDir {
			mode = fuse.S_IFDIR
		}
		entries = append(entries, fuse.DirEntry{
			Name: it.Name,
			Mode: mode,
		})
	}
	return fs.NewListDirStream(entries), 0
}

func (n *tnfsNode) Open(_ context.Context, flags uint32) (fs.FileHandle, uint32, syscall.Errno) {
	const writeMask = uint32(syscall.O_WRONLY | syscall.O_RDWR | syscall.O_CREAT | syscall.O_TRUNC | syscall.O_APPEND)
	if (flags & writeMask) != 0 {
		return nil, 0, syscall.EROFS
	}
	fd, err := n.client.OpenReadOnly(n.tnfsPath)
	if err != nil {
		return nil, 0, errToErrno(err)
	}
	return &tnfsFileHandle{client: n.client, fd: fd}, 0, 0
}

type tnfsFileHandle struct {
	client *tnfsClient
	fd     byte
}

var (
	_ fs.FileReader   = (*tnfsFileHandle)(nil)
	_ fs.FileReleaser = (*tnfsFileHandle)(nil)
)

func (h *tnfsFileHandle) Read(_ context.Context, dest []byte, off int64) (fuse.ReadResult, syscall.Errno) {
	n, err := h.client.ReadAt(h.fd, dest, off)
	if err != nil {
		return nil, errToErrno(err)
	}
	return fuse.ReadResultData(dest[:n]), 0
}

func (h *tnfsFileHandle) Release(_ context.Context) syscall.Errno {
	h.client.CloseFile(h.fd)
	return 0
}

func mountTNFS(mountPoint string, client *tnfsClient, debug bool) (*fuse.Server, error) {
	ttl := 1 * time.Second
	opts := &fs.Options{
		MountOptions: fuse.MountOptions{
			Name:    "tnfsfuse",
			FsName:  client.addr,
			Debug:   debug,
			Options: []string{"ro"},
		},
		EntryTimeout: &ttl,
		AttrTimeout:  &ttl,
	}
	root := &tnfsNode{
		client:   client,
		tnfsPath: "/",
	}
	return fs.Mount(mountPoint, root, opts)
}
