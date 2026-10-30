//go:build !windows

package main

import "syscall"

// pcNoFollowFlag makes open(2) fail on a symlink in the final component.
const pcNoFollowFlag = syscall.O_NOFOLLOW
