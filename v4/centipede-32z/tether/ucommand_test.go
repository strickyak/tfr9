package main

import (
	"bytes"
	"io"
	"os"
	"strings"
	"testing"
)

func captureStdout(f func()) string {
	oldStdout := os.Stdout
	r, w, _ := os.Pipe()
	os.Stdout = w

	f()

	w.Close()
	os.Stdout = oldStdout

	var buf bytes.Buffer
	io.Copy(&buf, r)
	return buf.String()
}

func TestSessionPhase(t *testing.T) {
	SetSessionPhase(T_Phase)
	if GetSessionPhase() != T_Phase {
		t.Fatalf("expected T Phase, got %v", GetSessionPhase())
	}

	SetSessionPhase(U_Phase)
	if GetSessionPhase() != U_Phase {
		t.Fatalf("expected U Phase, got %v", GetSessionPhase())
	}
}

func TestExecuteUCommandUndefined(t *testing.T) {
	phases := []SessionPhase{T_Phase, U_Phase}
	for _, phase := range phases {
		t.Run(string(phase), func(t *testing.T) {
			SetSessionPhase(phase)

			testCmd := "sample_missing_cmd"
			output := captureStdout(func() {
				ExecuteUCommand(testCmd)
			})

			expectedSub := "\r*** UCOMMAND not defined: [" + testCmd + "]\r"
			if !strings.Contains(output, expectedSub) {
				t.Fatalf("phase %s: expected stdout to contain %q, got %q", phase, expectedSub, output)
			}
		})
	}
}

func TestExecuteUCommandRegistered(t *testing.T) {
	var invokedArgs string
	var invoked bool

	RegisterUCommand("testcmd", func(args string) {
		invoked = true
		invokedArgs = args
	})
	defer delete(UCommands, "testcmd")

	output := captureStdout(func() {
		ExecuteUCommand("testcmd arg1 arg2")
	})

	if !invoked {
		t.Fatalf("expected testcmd to be invoked")
	}
	if invokedArgs != "arg1 arg2" {
		t.Fatalf("expected args 'arg1 arg2', got %q", invokedArgs)
	}
	if strings.Contains(output, "UCOMMAND not defined") {
		t.Fatalf("unexpected undefined error output: %q", output)
	}
}
