package bootmenu

import (
	"os"
	"testing"
)

func TestParseMenus(t *testing.T) {
	f, err := os.Open("../../misc/menus.txt")
	if err != nil {
		t.Fatalf("failed to open menus.txt: %v", err)
	}
	defer f.Close()

	config, err := Parse(f)
	if err != nil {
		t.Fatalf("Parse error: %v", err)
	}

	if len(config.Screens) != 6 {
		t.Errorf("Expected 6 screens, got %d", len(config.Screens))
	}

	for _, s := range config.Screens {
		if s.Name == "MEM" {
			if len(s.Constraints) != 2 {
				t.Errorf("MEM: Expected 2 constraints, got %d", len(s.Constraints))
			}
			foundBigram := false
			for _, w := range s.Widgets {
				if w.ID == "3" {
					if w.Type != "check" || w.FieldName != "bigram" {
						t.Errorf("MEM: Widget 3 mismatched type or fieldname: %q %q", w.Type, w.FieldName)
					}
					foundBigram = true
				}
			}
			if !foundBigram {
				t.Errorf("MEM: Widget 3 not found")
			}
		}

		if s.Name == "TRACE" {
			foundWrites := false
			for _, w := range s.Widgets {
				if w.ID == "8" {
					if w.Type != "decimal" || w.FieldName != "after-n-writes" {
						t.Errorf("TRACE: Widget 8 mismatched: %q %q", w.Type, w.FieldName)
					}
					foundWrites = true
				}
			}
			if !foundWrites {
				t.Errorf("TRACE: Widget 8 not found")
			}
		}

		if s.Name == "PRESET" {
			foundL := false
			for _, w := range s.Widgets {
				if w.ID == "L" {
					if w.Type != "action" || w.FieldName != "load-preset" {
						t.Errorf("PRESET: Widget L mismatched: %q %q", w.Type, w.FieldName)
					}
					foundL = true
				}
			}
			if !foundL {
				t.Errorf("PRESET: Widget L not found")
			}
		}
	}
}
