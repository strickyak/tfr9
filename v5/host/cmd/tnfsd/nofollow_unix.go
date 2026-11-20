//go:build !windows

package main

import "syscall"

const noFollowFlag = syscall.O_NOFOLLOW
