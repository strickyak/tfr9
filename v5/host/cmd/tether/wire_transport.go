package main

import (
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"strings"
	"sync"
	"time"
)

type execWireConn struct {
	cmd    *exec.Cmd
	stdin  io.WriteCloser
	stdout io.ReadCloser
	once   sync.Once
}

func (c *execWireConn) Read(p []byte) (int, error) {
	return c.stdout.Read(p)
}

func (c *execWireConn) Write(p []byte) (int, error) {
	return c.stdin.Write(p)
}

func (c *execWireConn) Close() error {
	var retErr error
	c.once.Do(func() {
		_ = c.stdin.Close()
		done := make(chan error, 1)
		go func() {
			done <- c.cmd.Wait()
		}()
		select {
		case retErr = <-done:
		case <-time.After(1500 * time.Millisecond):
			if c.cmd.Process != nil {
				_ = c.cmd.Process.Kill()
			}
			retErr = <-done
		}
		_ = c.stdout.Close()
	})
	return retErr
}

func splitCommandArgs(cmdline string) ([]string, error) {
	var args []string
	var cur strings.Builder
	inToken := false
	inSingle := false
	inDouble := false
	escaped := false

	for i := 0; i < len(cmdline); i++ {
		ch := cmdline[i]
		if escaped {
			cur.WriteByte(ch)
			escaped = false
			inToken = true
			continue
		}
		if inSingle {
			if ch == '\'' {
				inSingle = false
			} else {
				cur.WriteByte(ch)
			}
			inToken = true
			continue
		}
		if inDouble {
			if ch == '"' {
				inDouble = false
			} else if ch == '\\' && i+1 < len(cmdline) && (cmdline[i+1] == '"' || cmdline[i+1] == '\\') {
				escaped = true
			} else {
				cur.WriteByte(ch)
			}
			inToken = true
			continue
		}
		switch ch {
		case ' ', '\t', '\r', '\n':
			if inToken {
				args = append(args, cur.String())
				cur.Reset()
				inToken = false
			}
		case '\'':
			inSingle = true
			inToken = true
		case '"':
			inDouble = true
			inToken = true
		case '\\':
			escaped = true
			inToken = true
		default:
			cur.WriteByte(ch)
			inToken = true
		}
	}
	if inSingle || inDouble {
		return nil, fmt.Errorf("unterminated quote in exec wire command: %q", cmdline)
	}
	if inToken {
		args = append(args, cur.String())
	}
	if len(args) == 0 {
		return nil, fmt.Errorf("empty exec wire command")
	}
	return args, nil
}

func openNonSerialWire(portName string) (io.ReadWriteCloser, bool, error) {
	switch {
	case strings.HasPrefix(portName, "exec:"):
		argv, err := splitCommandArgs(strings.TrimPrefix(portName, "exec:"))
		if err != nil {
			return nil, true, err
		}
		cmd := exec.Command(argv[0], argv[1:]...)
		cmd.Stderr = os.Stderr
		stdin, err := cmd.StdinPipe()
		if err != nil {
			return nil, true, err
		}
		stdout, err := cmd.StdoutPipe()
		if err != nil {
			_ = stdin.Close()
			return nil, true, err
		}
		if err := cmd.Start(); err != nil {
			_ = stdin.Close()
			_ = stdout.Close()
			return nil, true, err
		}
		return &execWireConn{
			cmd:    cmd,
			stdin:  stdin,
			stdout: stdout,
		}, true, nil

	case strings.HasPrefix(portName, "tcp://"):
		addr := strings.TrimPrefix(portName, "tcp://")
		conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
		return conn, true, err

	case strings.HasPrefix(portName, "unix://"):
		addr := strings.TrimPrefix(portName, "unix://")
		conn, err := net.DialTimeout("unix", addr, 5*time.Second)
		return conn, true, err
	}
	return nil, false, nil
}
