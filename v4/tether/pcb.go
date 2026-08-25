package main

import (
	"bytes"
)

const (
	KIND_INT = 1
	KIND_STR = 2
	KIND_MSG = 3
)

func makeTag(fieldNum byte, kind byte) byte {
	return (fieldNum << 3) | kind
}

func putVarInt(buf *bytes.Buffer, val uint64) {
	for val >= 0x80 {
		buf.WriteByte(byte(val&0x7F) | 0x80)
		val >>= 7
	}
	buf.WriteByte(byte(val & 0x7F))
}

func getVarInt(buf []byte, offset *int) uint64 {
	var val uint64
	shift := 0
	for *offset < len(buf) {
		b := buf[*offset]
		*offset++
		val |= uint64(b&0x7F) << shift
		if (b & 0x80) == 0 {
			break
		}
		shift += 7
	}
	return val
}

func putInt(buf *bytes.Buffer, fieldNum byte, val int64) {
	buf.WriteByte(makeTag(fieldNum, KIND_INT))
	putVarInt(buf, uint64(val))
}

func putStr(buf *bytes.Buffer, fieldNum byte, val string) {
	buf.WriteByte(makeTag(fieldNum, KIND_STR))
	putVarInt(buf, uint64(len(val)))
	buf.WriteString(val)
}

func putBytes(buf *bytes.Buffer, fieldNum byte, val []byte) {
	buf.WriteByte(makeTag(fieldNum, KIND_STR))
	putVarInt(buf, uint64(len(val)))
	buf.Write(val)
}

type RpcRequest struct {
	Method string
	Serial int
	Path   string
	Path2  string
	Handle int
	Flags  int
	Length int
	Offset int
	Whence int
	Data   []byte
}

func DecodeRpcRequest(buf []byte) RpcRequest {
	var req RpcRequest
	offset := 0
	for offset < len(buf) {
		tag := buf[offset]
		offset++
		if tag == 0 {
			break
		}

		fieldNum := tag >> 3
		kind := tag & 7

		if kind == KIND_INT {
			val := int64(getVarInt(buf, &offset))
			switch fieldNum {
			case 2:
				req.Serial = int(val)
			case 5:
				req.Handle = int(val)
			case 6:
				req.Flags = int(val)
			case 7:
				req.Length = int(val)
			case 9:
				req.Offset = int(val)
			case 10:
				req.Whence = int(val)
			}
		} else if kind == KIND_STR {
			length := int(getVarInt(buf, &offset))
			sBuf := buf[offset : offset+length]
			offset += length
			switch fieldNum {
			case 1:
				req.Method = string(sBuf)
			case 3:
				req.Path = string(sBuf)
			case 4:
				req.Path2 = string(sBuf)
			case 8:
				req.Data = sBuf
			}
		}
	}
	return req
}

type RpcResponse struct {
	Status  int
	Message string
	Serial  int
	Handle  int
	Data    []byte
	Size    int
	IsDir   int
}

func EncodeRpcResponse(resp RpcResponse) []byte {
	var buf bytes.Buffer
	if resp.Status != 0 {
		putInt(&buf, 1, int64(resp.Status))
	}
	if resp.Message != "" {
		putStr(&buf, 1, resp.Message)
	}
	if resp.Serial != 0 {
		putInt(&buf, 2, int64(resp.Serial))
	}
	if resp.Handle != 0 {
		putInt(&buf, 3, int64(resp.Handle))
	}
	if len(resp.Data) > 0 {
		putBytes(&buf, 4, resp.Data)
	}
	if resp.Size != 0 {
		putInt(&buf, 5, int64(resp.Size))
	}
	if resp.IsDir != 0 {
		putInt(&buf, 6, int64(resp.IsDir))
	}
	buf.WriteByte(0) // Terminator
	return buf.Bytes()
}
