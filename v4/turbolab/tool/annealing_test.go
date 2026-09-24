package main

import (
	"math/rand"
	"strings"
	"testing"
)

func TestInitialParams(t *testing.T) {
	p := NewInitialParams(250)
	if p.Mhz != 250 {
		t.Fatalf("Expected MHZ=250, got %d", p.Mhz)
	}

	expected := map[string]int{
		"K1": 9, "K2": 19, "K3": 11, "K4": 8,
		"T1": 16, "T2": 0, "T3": 22, "T4": 3, "T5": 12,
	}

	for k, v := range expected {
		if p.Values[k] != v {
			t.Fatalf("Param %s: expected %d, got %d", k, v, p.Values[k])
		}
	}

	flag := p.TuningFlag()
	if flag != "MHZ=250,K1=9,K2=19,K3=11,K4=8,T1=16,T2=0,T3=22,T4=3,T5=12" {
		t.Fatalf("TuningFlag mismatch: got %q", flag)
	}
}

func TestGenerateNeighbor_Single(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	base := NewInitialParams(250)

	for i := 0; i < 50; i++ {
		neighbor := GenerateNeighbor(base, "single", rng)
		diffCount := 0
		for _, def := range DefaultParamDefs {
			oldV := base.Values[def.Name]
			newV := neighbor.Values[def.Name]

			// Check bounds
			if newV < def.Min || newV > def.Max {
				t.Fatalf("Param %s value %d out of bounds [%d, %d]", def.Name, newV, def.Min, def.Max)
			}

			diff := newV - oldV
			if diff != 0 {
				diffCount++
				if diff < -2 || diff > 2 {
					t.Fatalf("Param %s delta %d not in {-2, -1, 1, 2}", def.Name, diff)
				}
			}
		}
		if diffCount != 1 {
			t.Fatalf("Single mode must change exactly 1 parameter, changed %d", diffCount)
		}
	}
}

func TestGenerateNeighbor_Multi(t *testing.T) {
	rng := rand.New(rand.NewSource(123))
	base := NewInitialParams(250)

	for i := 0; i < 50; i++ {
		neighbor := GenerateNeighbor(base, "multi", rng)
		diffCount := 0
		for _, def := range DefaultParamDefs {
			oldV := base.Values[def.Name]
			newV := neighbor.Values[def.Name]

			// Check bounds
			if newV < def.Min || newV > def.Max {
				t.Fatalf("Param %s value %d out of bounds [%d, %d]", def.Name, newV, def.Min, def.Max)
			}

			diff := newV - oldV
			if diff != 0 {
				diffCount++
				if diff < -2 || diff > 2 {
					t.Fatalf("Param %s delta %d not in {-2, -1, 0, 1, 2}", def.Name, diff)
				}
			}
		}
		if diffCount == 0 {
			t.Fatalf("Multi mode must change at least 1 parameter")
		}
	}
}

func TestCanonicalPiVerification(t *testing.T) {
	// 4 lines of pi followed by fault
	fourLines := strings.Repeat(CanonicalPi+"\n", 4) + "*** PICO FAULT ***\n"
	if strings.Count(fourLines, CanonicalPi) != 4 {
		t.Fatalf("Expected 4 occurrences of CanonicalPi")
	}

	// 3 lines (incomplete)
	threeLines := strings.Repeat(CanonicalPi+"\n", 3)
	if strings.Count(threeLines, CanonicalPi) >= 4 {
		t.Fatalf("3 lines should not satisfy >= 4")
	}
}
