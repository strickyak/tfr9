package cobs

import (
	"fmt"
	"io"
	"log"
)

// StreamDecoder reads from an io.Reader continuously, splits the stream 
// on 0x00 frame delimiters, decodes the COBS packets, and sends them 
// to the outputChan.
func StreamDecoder(input io.Reader, outputChan chan<- []byte) error {
	buf := make([]byte, 4096)
	var currentPacket []byte
	
	for {
		n, err := input.Read(buf)
		if n > 0 {
			for _, b := range buf[:n] {
				if b == 0 {
					if len(currentPacket) > 0 {
						decoded, decodeErr := decodeRaw(currentPacket)
						if decodeErr == nil {
							if UseChecksums {
								if len(decoded) >= 2 && VerifyChecksum(decoded) {
									outputChan <- decoded[:len(decoded)-1] // strip checksum
								} else {
									log.Printf("COBS checksum mismatch, dropping packet len=%d", len(decoded))
								}
							} else {
								outputChan <- decoded
							}
						}
						// If decodeErr != nil, we simply drop the corrupted packet
						// and wait for the next 0x00 frame delimiter to resync.
						currentPacket = nil
					}
				} else {
					currentPacket = append(currentPacket, b)
				}
			}
		}
		if err != nil {
			if err == io.EOF {
				return nil
			}
			return err
		}
	}
}

// Decode performs standard COBS decoding, optionally verifying and stripping a checksum.
func Decode(data []byte) ([]byte, error) {
	raw, err := decodeRaw(data)
	if err != nil {
		return nil, err
	}
	if UseChecksums {
		if len(raw) < 2 || !VerifyChecksum(raw) {
			return nil, fmt.Errorf("cobs: checksum mismatch")
		}
		return raw[:len(raw)-1], nil
	}
	return raw, nil
}

// decodeRaw performs standard COBS decoding on the input data.
// The input must not contain the 0x00 frame delimiter.
func decodeRaw(data []byte) ([]byte, error) {
	if len(data) == 0 {
		return nil, nil
	}

	dst := make([]byte, 0, len(data))
	ptr := 0
	
	for ptr < len(data) {
		code := int(data[ptr])
		if code == 0 {
			return nil, fmt.Errorf("cobs: unexpected zero byte in payload")
		}
		ptr++
		
		copyLen := code - 1
		if ptr+copyLen > len(data) {
			return nil, fmt.Errorf("cobs: code byte points past end of payload")
		}
		
		for i := 0; i < copyLen; i++ {
			if data[ptr+i] == 0 {
				return nil, fmt.Errorf("cobs: unexpected zero byte in data")
			}
			dst = append(dst, data[ptr+i])
		}
		ptr += copyLen
		
		// Append implicit zero, unless it's the end of the packet and code < 0xFF
		if ptr < len(data) && code < 0xFF {
			dst = append(dst, 0)
		}
	}
	
	return dst, nil
}
