package main

import (
	"log"
	"os"
	"regexp"
	"strconv"
	"strings"
)

const Os9SectorSize = 256
const MaxDiskFiles = 16

type DiskFile struct {
	OsFile *os.File
}

var Files [MaxDiskFiles]DiskFile

var NumberedHPattern = regexp.MustCompile(`^[Hh]([0-9]):(.*)$`)

func OpenDisks(disks string) {
	for i, spec := range strings.Split(disks, ",") {
		if spec == "" {
			continue
		}

		hp := NumberedHPattern.FindStringSubmatch(spec)
		if hp != nil {
			filename := hp[2]
			j, err := strconv.Atoi(hp[1])
			if err != nil {
				log.Panicf("Not a number %q in disks spec %q: %v", hp[1], disks, err)
			}

			f, err := os.OpenFile(filename, os.O_RDWR, 0)
			if err != nil {
				log.Panicf("Cannot open [/H%d] file %q: %v", j, filename, err)
			}
			Files[j].OsFile = f
			log.Printf("Mounted /H%d on %q", j, filename)
		} else {
			filename := spec
			f, err := os.OpenFile(filename, os.O_RDWR, 0)
			if err != nil {
				log.Panicf("Cannot open [%d] file %q: %v", i, filename, err)
			}
			Files[i].OsFile = f
			log.Printf("Mounted /H%d on %q", i, filename)
		}
	}
}

func EmulateDiskWrite(pack []byte, channelToPico chan []byte) {
	var disk_param [4]byte
	for i := 0; i < 4; i++ {
		disk_param[i] = pack[i]
	}
	hnum := disk_param[0]
	AssertLT(hnum, MaxDiskFiles)

	lsn := (uint(disk_param[1]) << 16) | (uint(disk_param[2]) << 8) | uint(disk_param[3])

	_, err := Files[hnum].OsFile.Seek(Os9SectorSize*int64(lsn), 0)
	if err != nil {
		Panicf("Cannot seek")
	}
	Logf("C_DISK_WRITE num %x lsn %x", hnum, lsn)

	sector := make([]byte, Os9SectorSize)
	for i := 0; i < Os9SectorSize; i++ {
		sector[i] = pack[i+4]
	}

	_, err = Files[hnum].OsFile.Write(sector)
	if err != nil {
		Panicf("Cannot write")
	}

}

func EmulateDiskRead(pack []byte, channelToPico chan []byte) {
	var disk_param [4]byte
	for i := 0; i < 4; i++ {
		disk_param[i] = pack[i]
		//Logf("disk_param: %02x", disk_param[i])
	}
	hnum := disk_param[0]
	AssertLT(hnum, MaxDiskFiles)

	lsn := (uint(disk_param[1]) << 16) | (uint(disk_param[2]) << 8) | uint(disk_param[3])

	_, err := Files[hnum].OsFile.Seek(Os9SectorSize*int64(lsn), 0)
	if err != nil {
		Panicf("Cannot seek")
	}
	Logf("C_DISK_READ num %x lsn %x", hnum, lsn)

	sector := make([]byte, Os9SectorSize)
	_, err = Files[hnum].OsFile.Read(sector)
	if err != nil {
		Panicf("Cannot read")
	}

	WriteBytes(channelToPico, C_DISK_READ)
	PutSize(channelToPico, 4+256)
	WriteBytes(channelToPico, disk_param[:]...)
	WriteBytes(channelToPico, sector...)
}
