package main

import (
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"sync/atomic"
)

var tetherHandles = make(map[int]*os.File)
var tetherDirHandles = make(map[int]*os.File)
var nextHandle = 1

func HandleRpc(payload []byte, channelToPico chan []byte) {
	req := DecodeRpcRequest(payload)
	var ser uint64
	if *TETHER_LOG_USB {
		ser = atomic.AddUint64(&tetherLogUsbSerial, 1)
		log.Printf("_%d RPC IN: serial=%d, method=%s, path=%q, handle=%d, offset=%d, len=%d", ser, req.Serial, req.Method, req.Path, req.Handle, req.Offset, req.Length)
		fmt.Printf("(_%d ", ser)
	}
	resp := handleRpcReq(req)

	encodedResp := EncodeRpcResponse(resp)
	packet := append([]byte{T_RPC}, encodedResp...)

	if *TETHER_LOG_USB {
		serOut := atomic.AddUint64(&tetherLogUsbSerial, 1)
		log.Printf("_%d RPC OUT: serial=%d, status=%d, handle=%d, size=%d, is_dir=%d, data_len=%d", serOut, resp.Serial, resp.Status, resp.Handle, resp.Size, resp.IsDir, len(resp.Data))
		fmt.Printf(" _%d)", serOut)
	}
	WriteBytes(channelToPico, packet...)
}

// maxRpcRead bounds one "read" so a bad request cannot make the tether allocate gigabytes.
const maxRpcRead = 64 * 1024

// handleRpcReq serves one /pc request.  All paths go through pcResolve (see pcfs.go): the root is
// verified on every request, and symlinks and special files are invisible.
func handleRpcReq(req RpcRequest) RpcResponse {
	var resp RpcResponse
	resp.Serial = req.Serial

	fail := func(err error) {
		resp.Status = 1
		resp.Message = err.Error()
	}
	// Methods that need a path resolve it first.
	var realPath string
	switch req.Method {
	case "stat", "dir_open", "remove":
		p, err := pcResolve(req.Path, pcMustExist)
		if err != nil {
			fail(err)
			return resp
		}
		realPath = p
	case "open":
		final := pcMustExist
		if (req.Flags & 0x0100) != 0 { // LFS_O_CREAT
			final = pcMayCreate
		}
		p, err := pcResolve(req.Path, final)
		if err != nil {
			fail(err)
			return resp
		}
		realPath = p
	case "mkdir":
		p, err := pcResolve(req.Path, pcMayCreate)
		if err != nil {
			fail(err)
			return resp
		}
		realPath = p
	case "rename":
		p, err := pcResolve(req.Path, pcMustExist)
		if err != nil {
			fail(err)
			return resp
		}
		realPath = p
	}

	switch req.Method {
	case "stat":
		info, err := os.Stat(realPath)
		if err != nil {
			resp.Status = 1
			resp.Message = err.Error()
		} else {
			resp.Status = 0
			resp.Size = int(info.Size())
			if info.IsDir() {
				resp.IsDir = 1
			}
			resp.Data = []byte(info.Name())
		}
	case "open":
		// Simplified flags mapping. LFS flags:
		// LFS_O_RDONLY = 1, LFS_O_WRONLY = 2, LFS_O_RDWR = 3, LFS_O_CREAT = 0x0100
		var osFlags int
		if (req.Flags & 3) == 1 {
			osFlags = os.O_RDONLY
		} else if (req.Flags & 3) == 2 {
			osFlags = os.O_WRONLY
		} else {
			osFlags = os.O_RDWR
		}
		if (req.Flags & 0x0100) != 0 {
			osFlags |= os.O_CREATE
		}
		if (req.Flags & 0x0200) != 0 { // LFS_O_EXCL
			osFlags |= os.O_EXCL
		}
		if (req.Flags & 0x0400) != 0 { // LFS_O_TRUNC
			osFlags |= os.O_TRUNC
		}
		if (req.Flags & 0x0800) != 0 { // LFS_O_APPEND
			osFlags |= os.O_APPEND
		}

		f, err := pcOpenFile(realPath, osFlags, 0644)
		if err != nil {
			resp.Status = 1
			resp.Message = err.Error()
		} else {
			h := nextHandle
			nextHandle++
			tetherHandles[h] = f
			resp.Status = 0
			resp.Handle = h
		}
	case "read":
		f, ok := tetherHandles[req.Handle]
		if !ok {
			resp.Status = 1
			resp.Message = "Invalid handle"
		} else {
			length := req.Length
			if length < 0 || length > maxRpcRead {
				length = maxRpcRead
			}
			buf := make([]byte, length)
			n, err := f.Read(buf)
			if err != nil && err.Error() != "EOF" {
				resp.Status = 1
				resp.Message = err.Error()
			} else {
				resp.Status = 0
				resp.Data = buf[:n]
			}
		}
	case "write":
		f, ok := tetherHandles[req.Handle]
		if !ok {
			resp.Status = 1
			resp.Message = "Invalid handle"
		} else {
			_, err := f.Write(req.Data)
			if err != nil {
				resp.Status = 1
				resp.Message = err.Error()
			} else {
				resp.Status = 0
			}
		}
	case "seek":
		f, ok := tetherHandles[req.Handle]
		if !ok {
			resp.Status = 1
			resp.Message = "Invalid handle"
		} else {
			offset, err := f.Seek(int64(req.Offset), req.Whence)
			if err != nil {
				resp.Status = 1
				resp.Message = err.Error()
			} else {
				resp.Status = 0
				resp.Size = int(offset)
			}
		}
	case "close":
		f, ok := tetherHandles[req.Handle]
		if ok {
			f.Close()
			delete(tetherHandles, req.Handle)
		}
		resp.Status = 0
	case "dir_open":
		f, err := pcOpenFile(realPath, os.O_RDONLY, 0)
		if err == nil {
			if fi, e := f.Stat(); e != nil || !fi.IsDir() {
				f.Close()
				err = errors.New("not a directory")
			}
		}
		if err != nil {
			resp.Status = 1
			resp.Message = err.Error()
		} else {
			h := nextHandle
			nextHandle++
			tetherDirHandles[h] = f
			resp.Status = 0
			resp.Handle = h
		}
	case "dir_read":
		f, ok := tetherDirHandles[req.Handle]
		if !ok {
			resp.Status = 1
			resp.Message = "Invalid handle"
		} else {
			var entry os.DirEntry
			var err error
			for {
				var entries []os.DirEntry
				entries, err = f.ReadDir(1)
				if err != nil || len(entries) == 0 {
					break
				}
				if pcVisible(entries[0]) { // symlinks, FIFOs, sockets, devices are not shown
					entry = entries[0]
					break
				}
			}
			if err != nil && !errors.Is(err, io.EOF) {
				resp.Status = 1
				resp.Message = err.Error()
			} else if entry == nil {
				resp.Status = 0
				resp.Data = []byte{}
			} else {
				resp.Status = 0
				resp.Data = []byte(entry.Name())
				if entry.IsDir() {
					resp.IsDir = 1
				}
				info, err := entry.Info()
				if err == nil {
					resp.Size = int(info.Size())
				}
			}
		}
	case "dir_close":
		f, ok := tetherDirHandles[req.Handle]
		if ok {
			f.Close()
			delete(tetherDirHandles, req.Handle)
		}
		resp.Status = 0
	case "mkdir":
		err := os.Mkdir(realPath, 0755)
		if err != nil {
			resp.Status = 1
			resp.Message = err.Error()
		} else {
			resp.Status = 0
		}
	case "remove":
		err := os.Remove(realPath)
		if err != nil {
			resp.Status = 1
			resp.Message = err.Error()
		} else {
			resp.Status = 0
		}
	case "rename":
		realPath2, err := pcResolve(req.Path2, pcMayCreate)
		if err == nil {
			err = os.Rename(realPath, realPath2)
		}
		if err != nil {
			resp.Status = 1
			resp.Message = err.Error()
		} else {
			resp.Status = 0
		}
	default:
		resp.Status = 1
		resp.Message = fmt.Sprintf("Unknown RPC method: %s", req.Method)
	}

	return resp
}
