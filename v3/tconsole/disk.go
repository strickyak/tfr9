package main

import (
	"log"
	"os"
	"regexp"
	"strconv"
	"strings"
)

const Os9SectorSize = 256
const MaxDiskFiles = 128
const FloppyDeviceStart = 64

type DiskFile struct {
	OsFile *os.File
}

var Files [MaxDiskFiles]DiskFile

var NumberedHPattern = regexp.MustCompile(`^[HhFf]([0-9]):(.*)$`)

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

			if spec[0] == 'f' || spec[0] == 'F' {
				j += FloppyDeviceStart
			}

			f, err := os.OpenFile(filename, os.O_RDWR, 0)
			if err != nil {
				log.Panicf("Cannot open disk %d file %q: %v", j, filename, err)
			}
			Files[j].OsFile = f
			log.Printf("Mounted disk %d on %q", j, filename)
		} else {
			filename := spec
			f, err := os.OpenFile(filename, os.O_RDWR, 0)
			if err != nil {
				log.Panicf("Cannot open disk %d file %q: %v", i, filename, err)
			}
			Files[i].OsFile = f
			log.Printf("Mounted disk %d on %q", i, filename)
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
		Panicf("EmulateDiskWrite: Cannot seek hnum=%d lsn=%d param=%v", hnum, lsn, disk_param)
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

func EmulateDiskRead(disk_param []byte, channelToPico chan []byte) {
	var hnum byte
	var lsn uint

    for i := 0; i < len(disk_param); i++ {
        Logf("EmulateDiskRead: disk_param[%x]: %02x", i, disk_param[i])
    }

	switch len(disk_param) {
	case 4: // TFR9 only
		hnum = disk_param[0]
		AssertLT(hnum, MaxDiskFiles)

		lsn = (uint(disk_param[1]) << 16) | (uint(disk_param[2]) << 8) | uint(disk_param[3])

	case 5: // centipede0 only
        if disk_param[0] != 'f' {
            Panicf("unknown EmulateDiskRead packet len 5: % 2x", disk_param)
        }
        if disk_param[1] != 0x80 {
            Panicf("unknown EmulateDiskRead packet len 5: % 2x", disk_param)
        }
        hnum = FloppyDeviceStart
        switch {
        case (disk_param[2] & 1) != 0:
            hnum += 0
        case (disk_param[2] & 2) != 0:
            hnum += 1
        case (disk_param[2] & 4) != 0:
            hnum += 2
        case (disk_param[2] & 0x40) != 0:
            hnum += 3
        default:
            Panicf("unknown EmulateDiskRead packet hnum: % 2x", disk_param)
        }
        lsn = 18 * uint(disk_param[3]) + uint(disk_param[4]) - 1;

	default:
		Panicf("unknown EmulateDiskRead packet % 2x", disk_param)
	}

	_, err := Files[hnum].OsFile.Seek(Os9SectorSize*int64(lsn), 0)
	if err != nil {
		Panicf("EmulateDiskRead: Cannot seek hnum=%d lsn=%d param=% 2x", hnum, lsn, disk_param)
	}
	Logf("C_DISK_READ num $%x lsn $%x=%d.", hnum, lsn, lsn);

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
