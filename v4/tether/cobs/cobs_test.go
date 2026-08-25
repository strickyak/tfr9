package cobs

import (
	"bytes"
	"io"
	"reflect"
	"testing"
)

func TestEncodeDecode(t *testing.T) {
	UseChecksums = false // Test raw COBS without checksums
	defer func() { UseChecksums = true }()

	tests := []struct {
		name     string
		original []byte
		encoded  []byte
	}{
		{
			name:     "no zeros",
			original: []byte{0x11, 0x22, 0x33, 0x44},
			encoded:  []byte{0x05, 0x11, 0x22, 0x33, 0x44},
		},
		{
			name:     "contains zeros",
			original: []byte{0x11, 0x00, 0x22, 0x00},
			encoded:  []byte{0x02, 0x11, 0x02, 0x22, 0x01},
		},
		{
			name:     "starts and ends with zeros",
			original: []byte{0x00, 0x11, 0x00},
			encoded:  []byte{0x01, 0x02, 0x11, 0x01},
		},
		{
			name:     "254 non-zero bytes",
			original: bytes.Repeat([]byte{0x11}, 254),
			encoded:  append(append([]byte{0xFF}, bytes.Repeat([]byte{0x11}, 254)...), 0x01),
		},
		{
			name:     "255 non-zero bytes",
			original: bytes.Repeat([]byte{0x11}, 255),
			encoded:  append(append(append([]byte{0xFF}, bytes.Repeat([]byte{0x11}, 254)...), 0x02), 0x11),
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			enc := Encode(tt.original)
			if !reflect.DeepEqual(enc, tt.encoded) {
				t.Errorf("Encode() = %x, want %x", enc, tt.encoded)
			}

			dec, err := Decode(tt.encoded)
			if err != nil {
				t.Fatalf("Decode() error = %v", err)
			}
			if !reflect.DeepEqual(dec, tt.original) {
				t.Errorf("Decode() = %x, want %x", dec, tt.original)
			}
		})
	}
}

func TestStream(t *testing.T) {
	UseChecksums = false // Test raw COBS without checksums
	defer func() { UseChecksums = true }()

	inputData := [][]byte{
		{0x11, 0x22},
		{0x00, 0x33},
		{0x44, 0x55, 0x66},
	}

	inChan := make(chan []byte, len(inputData))
	for _, p := range inputData {
		inChan <- p
	}
	close(inChan)

	var buf bytes.Buffer
	err := StreamEncoder(inChan, &buf)
	if err != nil {
		t.Fatalf("StreamEncoder error = %v", err)
	}

	outChan := make(chan []byte, 10)
	go func() {
		err := StreamDecoder(&buf, outChan)
		if err != nil && err != io.EOF {
			t.Errorf("StreamDecoder error = %v", err)
		}
		close(outChan)
	}()

	var received [][]byte
	for p := range outChan {
		received = append(received, p)
	}

	if !reflect.DeepEqual(received, inputData) {
		t.Errorf("Stream received %x, want %x", received, inputData)
	}
}

func TestChecksum(t *testing.T) {
	data := []byte{0x11, 0x22, 0x33}
	ck := Checksum(data)
	withCk := append(append([]byte{}, data...), ck)
	if !VerifyChecksum(withCk) {
		t.Errorf("VerifyChecksum failed for %x (checksum %02x)", data, ck)
	}
}

func TestEncodeDecodeWithChecksums(t *testing.T) {
	UseChecksums = true
	defer func() { UseChecksums = true }()

	original := []byte{0x11, 0x22, 0x00, 0x33}
	encoded := Encode(original)
	decoded, err := Decode(encoded)
	if err != nil {
		t.Fatalf("Decode error: %v", err)
	}
	if !reflect.DeepEqual(decoded, original) {
		t.Errorf("Round-trip with checksums: got %x, want %x", decoded, original)
	}
}
