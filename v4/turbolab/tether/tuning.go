package main

import (
	"encoding/binary"
	"fmt"
	"strconv"
	"strings"
)

// TuningParams holds the runtime timing configuration for the RP2350 Hamster PIO engine.
type TuningParams struct {
	Mhz uint32
	K   [10]uint32 // 1-indexed K1..K9
	T   [10]uint32 // 1-indexed T1..T9
}

// DefaultTuningParams returns the calibrated defaults for 250MHz / 3.3MHz 6309 bus.
func DefaultTuningParams() TuningParams {
	return TuningParams{
		Mhz: 250,
		K:   [10]uint32{0, 9, 19, 11, 8, 0, 0, 0, 0, 0},
		T:   [10]uint32{0, 16, 0, 22, 3, 12, 0, 0, 0, 0},
	}
}

// ParseTuningFlag parses a tuning string which can be a comma-separated list of numbers
// (e.g. "MHZ,K1,K2,T1,T2,T3,T4,T5" or "MHZ,K1,K2,K3,K4,T1,T2,T3,T4,T5")
// or key=val pairs (e.g. "MHZ=200,K1=8,T1=13").
func ParseTuningFlag(tuningStr string) (TuningParams, error) {
	tp := DefaultTuningParams()
	if strings.TrimSpace(tuningStr) == "" {
		return tp, nil
	}

	tokens := strings.Split(tuningStr, ",")
	var bareNumbers []uint32

	type namedToken struct {
		key string
		val uint32
	}
	var named []namedToken

	for _, token := range tokens {
		t := strings.TrimSpace(token)
		if t == "" {
			continue
		}
		if strings.Contains(t, "=") {
			parts := strings.SplitN(t, "=", 2)
			k := strings.ToUpper(strings.TrimSpace(parts[0]))
			v, err := strconv.ParseUint(strings.TrimSpace(parts[1]), 0, 32)
			if err != nil {
				return tp, fmt.Errorf("invalid value in %q: %w", t, err)
			}
			named = append(named, namedToken{key: k, val: uint32(v)})
		} else {
			v, err := strconv.ParseUint(t, 0, 32)
			if err != nil {
				return tp, fmt.Errorf("invalid number %q: %w", t, err)
			}
			bareNumbers = append(bareNumbers, uint32(v))
		}
	}

	// Handle positional bare numbers
	if len(bareNumbers) > 0 {
		if len(bareNumbers) == 10 {
			// All 10 parameters: MHZ, K1, K2, K3, K4, T1, T2, T3, T4, T5
			tp.Mhz = bareNumbers[0]
			tp.K[1] = bareNumbers[1]
			tp.K[2] = bareNumbers[2]
			tp.K[3] = bareNumbers[3]
			tp.K[4] = bareNumbers[4]
			tp.T[1] = bareNumbers[5]
			tp.T[2] = bareNumbers[6]
			tp.T[3] = bareNumbers[7]
			tp.T[4] = bareNumbers[8]
			tp.T[5] = bareNumbers[9]
		} else {
			// Positional order matching user prompt: MHZ, K1, K2, T1, T2, T3, T4, T5
			positions := []func(val uint32){
				func(v uint32) { tp.Mhz = v },
				func(v uint32) { tp.K[1] = v },
				func(v uint32) { tp.K[2] = v },
				func(v uint32) { tp.T[1] = v },
				func(v uint32) { tp.T[2] = v },
				func(v uint32) { tp.T[3] = v },
				func(v uint32) { tp.T[4] = v },
				func(v uint32) { tp.T[5] = v },
			}
			for i, v := range bareNumbers {
				if i < len(positions) {
					positions[i](v)
				}
			}
		}
	}

	// Apply any named parameters (these override or complement bare numbers)
	for _, nt := range named {
		switch nt.key {
		case "MHZ":
			tp.Mhz = nt.val
		case "K1":
			tp.K[1] = nt.val
		case "K2":
			tp.K[2] = nt.val
		case "K3":
			tp.K[3] = nt.val
		case "K4":
			tp.K[4] = nt.val
		case "K5":
			tp.K[5] = nt.val
		case "K6":
			tp.K[6] = nt.val
		case "K7":
			tp.K[7] = nt.val
		case "K8":
			tp.K[8] = nt.val
		case "K9":
			tp.K[9] = nt.val
		case "T1":
			tp.T[1] = nt.val
		case "T2":
			tp.T[2] = nt.val
		case "T3":
			tp.T[3] = nt.val
		case "T4":
			tp.T[4] = nt.val
		case "T5":
			tp.T[5] = nt.val
		case "T6":
			tp.T[6] = nt.val
		case "T7":
			tp.T[7] = nt.val
		case "T8":
			tp.T[8] = nt.val
		case "T9":
			tp.T[9] = nt.val
		default:
			return tp, fmt.Errorf("unknown tuning parameter name %q (expected MHZ, K1..K9, T1..T9)", nt.key)
		}
	}

	return tp, nil
}

// Encode converts the parameters into 84 bytes (21 little-endian uint32s: 1 mhz + 10 k + 10 t).
func (tp TuningParams) Encode() []byte {
	buf := make([]byte, 84)
	binary.LittleEndian.PutUint32(buf[0:4], tp.Mhz)
	for i := 1; i <= 9; i++ {
		binary.LittleEndian.PutUint32(buf[i*4:(i+1)*4], tp.K[i])
	}
	for i := 1; i <= 9; i++ {
		binary.LittleEndian.PutUint32(buf[(10+i)*4:(10+i+1)*4], tp.T[i])
	}
	return buf
}

func (tp TuningParams) String() string {
	return fmt.Sprintf("MHZ=%d, K1=%d, K2=%d, K3=%d, K4=%d, T1=%d, T2=%d, T3=%d, T4=%d, T5=%d",
		tp.Mhz, tp.K[1], tp.K[2], tp.K[3], tp.K[4], tp.T[1], tp.T[2], tp.T[3], tp.T[4], tp.T[5])
}
