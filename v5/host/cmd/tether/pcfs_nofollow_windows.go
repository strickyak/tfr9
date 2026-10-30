//go:build windows

package main

// Windows has no O_NOFOLLOW; pcResolve's Lstat check (symlinks and junctions are refused) and the
// SameFile re-check in pcOpenFile are what protect us there.
const pcNoFollowFlag = 0
