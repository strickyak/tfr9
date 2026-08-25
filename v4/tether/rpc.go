package main

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
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
	var resp RpcResponse
	resp.Serial = req.Serial

	// All PC paths are rooted at `*PC_DIR` (which defaults to `.`)
	realPath := filepath.Join(*PC_DIR, filepath.Clean("/"+req.Path))

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

		f, err := os.OpenFile(realPath, osFlags, 0644)
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
			buf := make([]byte, req.Length)
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
		f, err := os.Open(realPath)
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
			entries, err := f.ReadDir(1)
			if err != nil && err.Error() != "EOF" {
				resp.Status = 1
				resp.Message = err.Error()
			} else if len(entries) == 0 {
				resp.Status = 0
				resp.Data = []byte{}
			} else {
				entry := entries[0]
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
		realPath2 := filepath.Join(*PC_DIR, filepath.Clean("/"+req.Path2))
		err := os.Rename(realPath, realPath2)
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

	encodedResp := EncodeRpcResponse(resp)
	packet := append([]byte{T_RPC}, encodedResp...)
	
	if *TETHER_LOG_USB {
		serOut := atomic.AddUint64(&tetherLogUsbSerial, 1)
		log.Printf("_%d RPC OUT: serial=%d, status=%d, handle=%d, size=%d, is_dir=%d, data_len=%d", serOut, resp.Serial, resp.Status, resp.Handle, resp.Size, resp.IsDir, len(resp.Data))
		fmt.Printf(" _%d)", serOut)
	}
	WriteBytes(channelToPico, packet...)
}
