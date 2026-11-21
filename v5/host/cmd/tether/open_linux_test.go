//go:build linux

package main

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
	"testing"
	"time"
	"unsafe"

	"golang.org/x/sys/unix"
)

func openTestPty(t *testing.T) (*os.File, string) {
	t.Helper()
	ptmx, err := os.OpenFile("/dev/ptmx", os.O_RDWR|unix.O_NOCTTY, 0)
	if err != nil {
		t.Skipf("cannot open /dev/ptmx: %v", err)
	}
	sc, err := ptmx.SyscallConn()
	if err != nil {
		ptmx.Close()
		t.Fatalf("SyscallConn(/dev/ptmx): %v", err)
	}

	var ptyNum uint32
	var unlock int32
	var ioctlErr error
	sc.Control(func(fd uintptr) {
		if _, _, errno := unix.Syscall(unix.SYS_IOCTL, fd, unix.TIOCSPTLCK, uintptr(unsafe.Pointer(&unlock))); errno != 0 {
			ioctlErr = errno
			return
		}
		if _, _, errno := unix.Syscall(unix.SYS_IOCTL, fd, unix.TIOCGPTN, uintptr(unsafe.Pointer(&ptyNum))); errno != 0 {
			ioctlErr = errno
		}
	})
	if ioctlErr != nil {
		ptmx.Close()
		t.Fatalf("PTY ioctl setup failed: %v", ioctlErr)
	}
	return ptmx, fmt.Sprintf("/dev/pts/%d", ptyNum)
}

func TestOpenSerialExclusiveTIOCEXCL(t *testing.T) {
	if os.Geteuid() == 0 {
		t.Skip("CAP_SYS_ADMIN bypasses kernel TIOCEXCL; skipping root test")
	}
	ptmx, ptsPath := openTestPty(t)
	defer ptmx.Close()

	baseOpts := OpenSerialOptions{
		PortName:        ptsPath,
		BaudRate:        115200,
		DataBits:        8,
		StopBits:        1,
		MinimumReadSize: 1,
	}

	// 1. Exclusive open (default: NonExclusive=false)
	p1, err := OpenSerial(baseOpts)
	if err != nil {
		t.Fatalf("first OpenSerial(%s) failed: %v", ptsPath, err)
	}

	// 2. Second open while p1 is open must fail immediately with EBUSY
	p2, err := OpenSerial(baseOpts)
	if err == nil {
		p2.Close()
		p1.Close()
		t.Fatalf("expected second OpenSerial(%s) to fail with EBUSY, but it succeeded", ptsPath)
	}
	if !isSerialBusy(err) {
		p1.Close()
		t.Fatalf("expected isSerialBusy(err)=true, got err=%v", err)
	}

	// 3. Closing p1 releases TIOCEXCL so reopening succeeds
	if err := p1.Close(); err != nil {
		t.Fatalf("p1.Close() failed: %v", err)
	}
	p3, err := OpenSerial(baseOpts)
	if err != nil {
		t.Fatalf("reopen after Close() failed: %v", err)
	}
	p3.Close()

	// 4. NonExclusive=true (--nonexclusive) allows concurrent opens
	nonExclOpts := baseOpts
	nonExclOpts.NonExclusive = true
	n1, err := OpenSerial(nonExclOpts)
	if err != nil {
		t.Fatalf("first nonexclusive OpenSerial failed: %v", err)
	}
	defer n1.Close()

	n2, err := OpenSerial(nonExclOpts)
	if err != nil {
		t.Fatalf("second nonexclusive OpenSerial failed: %v", err)
	}
	defer n2.Close()
}

func TestJustConnectFailsFastOnEBUSY(t *testing.T) {
	if os.Geteuid() == 0 {
		t.Skip("CAP_SYS_ADMIN bypasses kernel TIOCEXCL; skipping root test")
	}
	ptmx, ptsPath := openTestPty(t)
	defer ptmx.Close()

	holder, err := OpenSerial(OpenSerialOptions{
		PortName:        ptsPath,
		BaudRate:        115200,
		DataBits:        8,
		StopBits:        1,
		MinimumReadSize: 1,
	})
	if err != nil {
		t.Fatalf("holder OpenSerial(%s) failed: %v", ptsPath, err)
	}
	defer holder.Close()

	// Run self test binary or build/tether if present to verify fast EBUSY exit (< 800ms, vs 3s retry loop)
	tetherBin := "../../../build/tether"
	if _, statErr := os.Stat(tetherBin); statErr != nil {
		t.Skip("build/tether not built yet")
	}
	start := time.Now()
	cmd := exec.Command(tetherBin, "-n", "-bind=", "-just-get-label", "-wire="+ptsPath)
	out, runErr := cmd.CombinedOutput()
	elapsed := time.Since(start)
	if runErr == nil {
		t.Fatalf("expected -just-get-label on busy port to fail, output=%q", string(out))
	}
	if elapsed >= 1*time.Second {
		t.Errorf("expected fast fail on EBUSY (<1s), took %v (output=%q)", elapsed, string(out))
	}
	if !strings.Contains(string(out), "busy") {
		t.Errorf("expected busy error message, got %q", string(out))
	}
}
