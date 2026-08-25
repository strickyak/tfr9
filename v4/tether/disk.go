package main

import (
	"flag"
	"log"
	"os"
	"regexp"
	"strconv"
	"strings"
)

var DOS_HACK = flag.Bool("dos_hack", false, "fix DOS command sector numbers")
var dos_count uint

const Os9SectorSize = 256
const MaxDiskFiles = 128
const FloppyDeviceStart = 64

type DiskFile struct {
	DoubleSided bool
	OsFile      *os.File
}

var Files [MaxDiskFiles]DiskFile

var NumberedHPattern = regexp.MustCompile(`^[HhFfDd]([0-9]):(.*)$`)

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
				Panicf("Not a number %q in disks spec %q: %v", hp[1], disks, err)
				panic(0)
			}

			if spec[0] == 'f' || spec[0] == 'F' {
				j += FloppyDeviceStart
			}
			if spec[0] == 'd' || spec[0] == 'D' {
				j += FloppyDeviceStart
				Files[j].DoubleSided = true
			}

			f, err := os.OpenFile(filename, os.O_RDWR, 0)
			if err != nil {
				Panicf("Cannot open disk %d file %q: %v", j, filename, err)
				panic(0)
			}
			Files[j].OsFile = f
			log.Printf("Mounted disk %d on %q", j, filename)
		} else {
			filename := spec
			f, err := os.OpenFile(filename, os.O_RDWR, 0)
			if err != nil {
				Panicf("Cannot open disk %d file %q: %v", i, filename, err)
				panic(0)
			}
			Files[i].OsFile = f
			log.Printf("Mounted disk %d on %q", i, filename)
		}
	}
}

func EmulateDiskWrite(disk_param []byte, channelToPico chan []byte) {
	log.Printf("EmulateDiskWrite: disk_param = [%d] % 3x", len(disk_param), disk_param)
	var hnum byte
	var lsn uint
	var sector []byte

	switch len(disk_param) {
	case 256 + 4:
		var disk_param [4]byte
		for i := 0; i < 4; i++ {
			disk_param[i] = disk_param[i]
		}
		hnum = disk_param[0]
		AssertLT(hnum, MaxDiskFiles)

		lsn = (uint(disk_param[1]) << 16) | (uint(disk_param[2]) << 8) | uint(disk_param[3])
		Logf("C_DISK_WRITE num %x lsn %x", hnum, lsn)

		sector = disk_param[4:]

	case 256 + 5:
		hnum = 0x40
		AssertLT(hnum, MaxDiskFiles)

		sectorsPerTrack := uint(Cond(Files[hnum].DoubleSided, 36, 18))
		dden_offset := uint(Cond((disk_param[2]&0x40) != 0, 18, 0))
		lsn = dden_offset + sectorsPerTrack*uint(disk_param[3]) + uint(disk_param[4]) - 1
		Logf("C_DISK_WRITE num %x lsn %x", hnum, lsn)
		sector = disk_param[5:]

	default:
		Panicf("EmulateDiskWrite: bad len(disk_param) = %d", len(disk_param))
		panic(0)
	}

	_, err := Files[hnum].OsFile.Seek(Os9SectorSize*int64(lsn), 0)
	if err != nil {
		Panicf("EmulateDiskWrite: Cannot seek hnum=%d lsn=%d param=% 3x", hnum, lsn, disk_param)
	}

	AssertEQ(len(sector), 256)

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
			Panicf("unknown EmulateDiskRead param 0 (want 0x66): % 2x", disk_param)
		}
		if disk_param[1] != 0x80 {
			Panicf("unknown EmulateDiskRead param 1 (want 0x80): % 2x", disk_param)
		}

		if *DOS_HACK && dos_count < 18 {
			hnum = 0
			lsn = 1224 + dos_count

			Logf("T_DISK_READ dev=%d. DOS_HACK(%d.)   lsn %d.", hnum, dos_count, lsn)
			dos_count++
		} else {
			hnum = FloppyDeviceStart
			switch {
			case (disk_param[2] & 1) != 0:
				hnum += 0
			case (disk_param[2] & 2) != 0:
				hnum += 1
			case (disk_param[2] & 4) != 0:
				hnum += 2
			default:
				Panicf("unknown EmulateDiskRead packet hnum: % 2x", disk_param)
			}
			sectorsPerTrack := uint(Cond(Files[hnum].DoubleSided, 36, 18))
			dden_offset := uint(Cond((disk_param[2]&0x40) != 0, 18, 0))
			lsn = dden_offset + sectorsPerTrack*uint(disk_param[3]) + uint(disk_param[4]) - 1

			Logf("T_DISK_READ dev=%d. latch=$%02x  track=%d. sect=%d.   lsn %d.", hnum, disk_param[2], disk_param[3], disk_param[4], lsn)
		}

	default:
		Panicf("unknown EmulateDiskRead packet % 2x", disk_param)
	}

	_, err := Files[hnum].OsFile.Seek(Os9SectorSize*int64(lsn), 0)
	if err != nil {
		Panicf("EmulateDiskRead: Cannot seek hnum=%d. lsn=%d. param=$%02x", hnum, lsn, disk_param)
	}

	sector := make([]byte, Os9SectorSize)
	_, err = Files[hnum].OsFile.Read(sector)
	if err != nil {
		log.Printf("EmulateDiskRead: Cannot read")
		packet := []byte{T_DISK_READ}
		sz := uint(len(disk_param)) + 256
		if sz < 64 {
			packet = append(packet, byte(128+sz))
		} else {
			packet = append(packet, byte(192+(sz>>6)), byte(128+(sz&63)))
		}
		packet = append(packet, disk_param...)
		packet = append(packet, sector...)
		WriteBytes(channelToPico, packet...)
		return
	}

	packet := []byte{T_DISK_READ}
	sz := uint(len(disk_param)) + 256
	if sz < 64 {
		packet = append(packet, byte(128+sz))
	} else {
		packet = append(packet, byte(192+(sz>>6)), byte(128+(sz&63)))
	}
	packet = append(packet, disk_param...)
	packet = append(packet, sector...)
	WriteBytes(channelToPico, packet...)
}
