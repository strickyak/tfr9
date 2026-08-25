package cobs

// UseChecksums controls whether COBS packets include a trailing checksum byte.
// When true, Encode appends a checksum and Decode verifies and strips it.
// Default is true to match the firmware's COBS_CHECKSUMS=1 default.
var UseChecksums = true

// Checksum computes the one's complement of the modulo-256 sum of the bytes.
// When appended to the data, the sum of all bytes (including checksum) is 0xFF.
func Checksum(data []byte) byte {
	var sum byte
	for _, b := range data {
		sum += b
	}
	return ^sum
}

// VerifyChecksum checks that the sum of all bytes (data + checksum) is 0xFF.
func VerifyChecksum(data []byte) bool {
	var sum byte
	for _, b := range data {
		sum += b
	}
	return sum == 0xFF
}
