package main

const Os9SectorSize = 256

func EmulateDiskWrite(fromUSB <-chan byte, channelToPico chan []byte) {
	//Logf("C_DISK_WRITE START (((")

	var disk_param [4]byte
	for i := 0; i < 4; i++ {
		disk_param[i] = <-fromUSB
		Logf("disk_param: %02x", disk_param[i])
	}
	AssertEQ(disk_param[0], 0)

	lsn := (uint(disk_param[1]) << 16) | (uint(disk_param[2]) << 8) | uint(disk_param[3])
	_, err := Files[0].Seek(Os9SectorSize*int64(lsn), 0)
	if err != nil {
		Fatalf("Cannot seek")
	}
	Logf("C_DISK_WRITE LSN %x", lsn)

	sector := make([]byte, Os9SectorSize)
	for i := 0; i < Os9SectorSize; i++ {
		sector[i] = <-fromUSB
	}

	_, err = Files[0].Write(sector)
	if err != nil {
		Fatalf("Cannot write")
	}

	//Logf("C_DISK_WRITE DONE )))")
}

func EmulateDiskRead(fromUSB <-chan byte, channelToPico chan []byte) {
	//Logf("C_DISK_READ START (((")

	var disk_param [4]byte
	for i := 0; i < 4; i++ {
		disk_param[i] = <-fromUSB
		Logf("disk_param: %02x", disk_param[i])
	}
	AssertEQ(disk_param[0], 0)

	lsn := (uint(disk_param[1]) << 16) | (uint(disk_param[2]) << 8) | uint(disk_param[3])
	_, err := Files[0].Seek(Os9SectorSize*int64(lsn), 0)
	if err != nil {
		Fatalf("Cannot seek")
	}
	Logf("C_DISK_READ LSN %x", lsn)

	sector := make([]byte, Os9SectorSize)
	_, err = Files[0].Read(sector)
	if err != nil {
		Fatalf("Cannot read")
	}
	//Logf("C_DISK_READ SECTOR % 3x ...", sector[:16])

	WriteBytes(channelToPico, C_DISK_READ)
	WriteBytes(channelToPico, disk_param[:]...)
	WriteBytes(channelToPico, sector...)

	//Logf("C_DISK_READ DONE )))")
}
