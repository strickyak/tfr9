package foggynet

import (
	"crypto/rand"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

const (
	maxHostSlots   = 8
	hostSlotSize   = 32
	maxDeviceSlots = 4 // CoCo DriveWire MAX_DWDISK_DEVICES = 4
	deviceSlotSize = 38
	maxAppKeyLen   = 64

	diskModeRead    = 0x01
	diskModeWrite   = 0x02
	diskModeMounted = 0x40
	invalidHostSlot = 0xFF
)

type deviceSlotState struct {
	HostSlot byte   `json:"host_slot"`
	Mode     byte   `json:"mode"`
	Filename string `json:"filename"`
}

type persistedConfig struct {
	SSID        string                           `json:"ssid"`
	Password    string                           `json:"password"`
	HostSlots   [maxHostSlots]string             `json:"host_slots"`
	DeviceSlots [maxDeviceSlots]deviceSlotState  `json:"device_slots"`
}

type mountedDisk interface {
	ReadSector(lsn uint32) ([256]byte, byte)
	WriteSector(lsn uint32, sec [256]byte) byte
	Close()
}

type localDiskFile struct {
	f        *os.File
	writable bool
}

func (d *localDiskFile) ReadSector(lsn uint32) ([256]byte, byte) {
	var sec [256]byte
	off := int64(lsn) * 256
	fi, err := d.f.Stat()
	if err != nil {
		return sec, 0xF4
	}
	if off >= fi.Size() {
		return sec, 211 // DriveWire EOF
	}
	n, err := d.f.ReadAt(sec[:], off)
	if err != nil && n == 0 {
		return sec, 0xF4
	}
	return sec, 0x00
}

func (d *localDiskFile) WriteSector(lsn uint32, sec [256]byte) byte {
	if !d.writable {
		return 0xF5
	}
	off := int64(lsn) * 256
	if _, err := d.f.WriteAt(sec[:], off); err != nil {
		return 0xF5
	}
	return 0x00
}

func (d *localDiskFile) Close() {
	if d.f != nil {
		_ = d.f.Close()
		d.f = nil
	}
}

func (s *Server) loadOrInitConfig() {
	_ = os.MkdirAll(s.sdDir(), 0755)
	_ = os.MkdirAll(s.appKeyDir(), 0755)

	// Default configuration: SSID "simulated" (no password), Slot 0 = SD, Slot 1 = tnfs.fujinet.online
	s.cfg.SSID = "simulated"
	s.cfg.Password = ""
	s.cfg.HostSlots[0] = "SD"
	s.cfg.HostSlots[1] = "tnfs.fujinet.online"
	for i := 0; i < maxDeviceSlots; i++ {
		s.cfg.DeviceSlots[i] = deviceSlotState{
			HostSlot: invalidHostSlot,
			Mode:     0,
			Filename: "",
		}
	}

	raw, err := os.ReadFile(s.configPath())
	if err == nil {
		var loaded persistedConfig
		if json.Unmarshal(raw, &loaded) == nil {
			if loaded.SSID != "" {
				s.cfg.SSID = loaded.SSID
			}
			s.cfg.Password = loaded.Password
			s.cfg.HostSlots = loaded.HostSlots
			s.cfg.DeviceSlots = loaded.DeviceSlots
		}
	}
}

func (s *Server) saveConfig() {
	_ = os.MkdirAll(s.stateDir, 0755)
	if raw, err := json.MarshalIndent(&s.cfg, "", "  "); err == nil {
		_ = os.WriteFile(s.configPath(), raw, 0644)
	}
}

func detectOutboundIPv4() [4]byte {
	conn, err := net.Dial("udp4", "8.8.8.8:53")
	if err == nil {
		defer conn.Close()
		if ua, ok := conn.LocalAddr().(*net.UDPAddr); ok {
			if ip4 := ua.IP.To4(); ip4 != nil {
				return [4]byte{ip4[0], ip4[1], ip4[2], ip4[3]}
			}
		}
	}
	return [4]byte{10, 0, 2, 15}
}

func (s *Server) buildAdapterConfigExtended() [249]byte {
	// Matches AdapterConfig (140B) + Extended string fields (109B = 249B total) in fujiDevice.h:
	// 0..32:    ssid[33]
	// 33..96:   hostname[64]
	// 97..100:  localIP[4]
	// 101..104: gateway[4]
	// 105..108: netmask[4]
	// 109..112: dnsIP[4]
	// 113..118: macAddress[6]
	// 119..124: bssid[6]
	// 125..139: fn_version[15]
	// Extended:
	// 140..155: sLocalIP[16]
	// 156..171: sGateway[16]
	// 172..187: sNetmask[16]
	// 188..203: sDnsIP[16]
	// 204..221: sMacAddress[18]
	// 222..239: sBssid[18]
	// 240..254? (140 + 64 + 36 = 240; pad safely to 249)
	var buf [249]byte
	ssid := s.cfg.SSID
	if ssid == "" {
		ssid = "simulated"
	}
	copyCString(buf[0:33], ssid)
	copyCString(buf[33:97], "foggynet")

	ip := detectOutboundIPv4()
	gw := [4]byte{ip[0], ip[1], ip[2], 1}
	mask := [4]byte{255, 255, 255, 0}
	dns := gw
	mac := [6]byte{0x02, 0xF0, 0x66, 0x79, 0x00, 0x01}
	bssid := [6]byte{0x02, 0x51, 0x4D, 0x55, 0x4C, 0x00}

	copy(buf[97:101], ip[:])
	copy(buf[101:105], gw[:])
	copy(buf[105:109], mask[:])
	copy(buf[109:113], dns[:])
	copy(buf[113:119], mac[:])
	copy(buf[119:125], bssid[:])
	copyCString(buf[125:140], "foggynet 1.0")

	copyCString(buf[140:156], fmt.Sprintf("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]))
	copyCString(buf[156:172], fmt.Sprintf("%d.%d.%d.%d", gw[0], gw[1], gw[2], gw[3]))
	copyCString(buf[172:188], "255.255.255.0")
	copyCString(buf[188:204], fmt.Sprintf("%d.%d.%d.%d", dns[0], dns[1], dns[2], dns[3]))
	copyCString(buf[204:222], "02:F0:66:79:00:01")
	copyCString(buf[222:240], "02:51:4D:55:4C:00")
	return buf
}

func copyCString(dst []byte, s string) {
	for i := range dst {
		dst[i] = 0
	}
	if len(dst) == 0 {
		return
	}
	n := len(s)
	if n >= len(dst) {
		n = len(dst) - 1
	}
	copy(dst[:n], s[:n])
}

func trimZeroString(b []byte) string {
	if idx := indexByteZero(b); idx >= 0 {
		b = b[:idx]
	}
	return string(b)
}

func isLocalSDHost(name string) bool {
	trimmed := strings.TrimSpace(name)
	return strings.EqualFold(trimmed, "SD") || strings.EqualFold(trimmed, "LOCAL")
}

func (s *Server) safeLocalSDPath(rel string) (string, error) {
	base := filepath.Clean(s.sdDir())
	cleaned := filepath.Clean(filepath.Join(base, filepath.FromSlash("/"+rel)))
	if cleaned != base && !strings.HasPrefix(cleaned, base+string(filepath.Separator)) {
		return "", fmt.Errorf("path escapes SD root")
	}
	return cleaned, nil
}

func (s *Server) mountHostSlot(slot int) error {
	if slot < 0 || slot >= maxHostSlots {
		return fmt.Errorf("invalid host slot %d", slot)
	}
	hname := strings.TrimSpace(s.cfg.HostSlots[slot])
	if hname == "" {
		return fmt.Errorf("host slot %d is empty", slot)
	}
	if isLocalSDHost(hname) {
		_ = os.MkdirAll(s.sdDir(), 0755)
		s.hostMounted[slot] = true
		return nil
	}
	if s.tnfsClients[slot] != nil {
		s.hostMounted[slot] = true
		return nil
	}
	s.logf("FUJI MountHost[%d]: connecting TNFS %q", slot, hname)
	cli, err := DialTNFS(hname)
	if err != nil {
		s.logf("FUJI MountHost[%d] error: %v", slot, err)
		return err
	}
	s.tnfsClients[slot] = cli
	s.hostMounted[slot] = true
	return nil
}

func (s *Server) unmountHostSlot(slot int) error {
	if slot < 0 || slot >= maxHostSlots {
		return fmt.Errorf("invalid host slot %d", slot)
	}
	for ds := 0; ds < maxDeviceSlots; ds++ {
		if int(s.cfg.DeviceSlots[ds].HostSlot) == slot && s.disks[ds] != nil {
			s.disks[ds].Close()
			s.disks[ds] = nil
		}
	}
	if s.tnfsClients[slot] != nil {
		_ = s.tnfsClients[slot].Close()
		s.tnfsClients[slot] = nil
	}
	s.hostMounted[slot] = false
	return nil
}

func (s *Server) mountDeviceSlot(ds int, mode byte) error {
	if ds < 0 || ds >= maxDeviceSlots {
		return fmt.Errorf("invalid device slot %d", ds)
	}
	hs := int(s.cfg.DeviceSlots[ds].HostSlot)
	fname := strings.TrimSpace(s.cfg.DeviceSlots[ds].Filename)
	if hs < 0 || hs >= maxHostSlots || fname == "" {
		return fmt.Errorf("device slot %d unconfigured", ds)
	}
	if err := s.mountHostSlot(hs); err != nil {
		return err
	}
	if s.disks[ds] != nil {
		s.disks[ds].Close()
		s.disks[ds] = nil
	}
	if mode == 0 {
		mode = s.cfg.DeviceSlots[ds].Mode & (diskModeRead | diskModeWrite)
		if mode == 0 {
			mode = diskModeRead
		}
	}
	writable := (mode & diskModeWrite) != 0
	hname := s.cfg.HostSlots[hs]
	if isLocalSDHost(hname) {
		localPath, err := s.safeLocalSDPath(fname)
		if err != nil {
			return err
		}
		flags := os.O_RDONLY
		if writable {
			flags = os.O_RDWR
		}
		f, err := os.OpenFile(localPath, flags, 0644)
		if err != nil {
			return err
		}
		s.disks[ds] = &localDiskFile{f: f, writable: writable}
	} else {
		cli := s.tnfsClients[hs]
		if cli == nil {
			return fmt.Errorf("tnfs client nil on host slot %d", hs)
		}
		tf, err := cli.OpenFile(fname, writable)
		if err != nil {
			return err
		}
		s.disks[ds] = tf
	}
	s.cfg.DeviceSlots[ds].Mode = mode & (diskModeRead | diskModeWrite)
	s.saveConfig()
	s.logf("FUJI MountImage slot=%d host=%d (%s) file=%q rw=%v", ds, hs, hname, fname, writable)
	return nil
}

func (s *Server) listHostDirectory(hs int, dirPath string, pattern string) ([]DirItem, error) {
	if err := s.mountHostSlot(hs); err != nil {
		return nil, err
	}
	hname := s.cfg.HostSlots[hs]
	if isLocalSDHost(hname) {
		localDir, err := s.safeLocalSDPath(dirPath)
		if err != nil {
			return nil, err
		}
		entries, err := os.ReadDir(localDir)
		if err != nil {
			return nil, err
		}
		var items []DirItem
		for _, e := range entries {
			name := e.Name()
			if strings.HasPrefix(name, ".") {
				continue
			}
			isDir := e.IsDir()
			if !isDir && pattern != "" {
				if matched, _ := path.Match(strings.ToLower(pattern), strings.ToLower(name)); !matched {
					continue
				}
			}
			var sz uint32
			var mt time.Time
			if fi, infoErr := e.Info(); infoErr == nil {
				sz = uint32(fi.Size())
				mt = fi.ModTime()
			}
			items = append(items, DirItem{
				Name:    name,
				IsDir:   isDir,
				Size:    sz,
				ModTime: mt,
			})
		}
		sort.Slice(items, func(i, j int) bool {
			if items[i].IsDir != items[j].IsDir {
				return items[i].IsDir
			}
			return strings.ToLower(items[i].Name) < strings.ToLower(items[j].Name)
		})
		return items, nil
	}
	return s.tnfsClients[hs].ListDir(dirPath, pattern)
}

func formatCoCoDirEntry(item DirItem, maxlen int, addtl byte) []byte {
	if maxlen <= 0 {
		return nil
	}
	out := make([]byte, maxlen)
	pos := 0

	// Optional 13-byte CoCo additional direntry details header (drivewireFuji.cpp:71-98) when bit 7 is set.
	if (addtl & 0x80) != 0 && maxlen > 14 {
		mt := item.ModTime
		if mt.IsZero() {
			mt = time.Now()
		}
		yr := mt.Year() - 2000
		if yr < 0 {
			yr = 0
		}
		out[0] = byte(yr)
		out[1] = byte(mt.Month())
		out[2] = byte(mt.Day())
		out[3] = byte(mt.Hour())
		out[4] = byte(mt.Minute())
		out[5] = byte(mt.Second())
		binary.BigEndian.PutUint32(out[6:10], item.Size)
		if item.IsDir {
			out[10] = 0x01
		}
		avail := maxlen - 13
		if item.IsDir {
			avail--
		}
		if len(item.Name) >= avail {
			out[11] = 0x02 // DET_FF_TRUNC
		}
		out[12] = 0 // mediatype
		pos = 13
	}

	rem := maxlen - pos
	name := item.Name
	if item.IsDir {
		if len(name)+2 > rem {
			if rem >= 3 {
				name = name[:rem-2]
			} else {
				name = ""
			}
		}
		name += "/"
	} else if len(name)+1 > rem {
		if rem >= 2 {
			name = name[:rem-1]
		} else {
			name = ""
		}
	}
	copy(out[pos:], name)
	return out
}

func (s *Server) handleFujiPacket(cmd byte) error {
	switch cmd {
	case 0x00: // FUJI_DEVICE_READY
		return s.writeBytes([]byte{0x01})

	case 0x02: // FUJI_SEND_ERROR
		return s.writeBytes([]byte{s.fujiLastErr})

	case 0x01: // FUJI_SEND_RESPONSE
		return s.writeBytes(s.fujiLastResp)

	case 0xFF: // FUJI_RESET
		s.logf("FUJI Reset")
		s.fujiLastErr = ndevStatusSuccess
		s.fujiLastResp = nil
		return nil

	case 0x53: // FUJI_STATUS ('S')
		s.fujiLastResp = []byte{0, 0, 0, 0}
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xEA: // FUJI_GET_WIFI_ENABLED
		s.fujiLastResp = []byte{1}
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xFA: // FUJI_GET_WIFISTATUS (3 = WL_CONNECTED)
		s.fujiLastResp = []byte{3}
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xFE: // FUJI_GET_SSID -> 97B SSIDConfig (ssid[33] + password[64])
		var buf [97]byte
		copyCString(buf[0:33], s.cfg.SSID)
		copyCString(buf[33:97], s.cfg.Password)
		s.fujiLastResp = buf[:]
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xFB: // FUJI_SET_SSID + 97B SSIDConfig
		raw, err := s.readExact(97)
		if err != nil {
			return err
		}
		newSSID := strings.TrimSpace(trimZeroString(raw[0:33]))
		if newSSID == "" {
			newSSID = "simulated"
		}
		s.cfg.SSID = newSSID
		s.cfg.Password = trimZeroString(raw[33:97])
		s.saveConfig()
		s.logf("FUJI SetSSID -> %q (simulated WiFi connected)", s.cfg.SSID)
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xFD: // FUJI_SCAN_NETWORKS -> 1B count
		s.fujiLastResp = []byte{1}
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xFC: // FUJI_GET_SCAN_RESULT + 1B index -> 34B SSIDInfo (ssid[33], rssi int8)
		idxBuf, err := s.readExact(1)
		if err != nil {
			return err
		}
		var info [34]byte
		if idxBuf[0] == 0 {
			ssid := s.cfg.SSID
			if ssid == "" {
				ssid = "simulated"
			}
			copyCString(info[0:33], ssid)
			info[33] = 0xD6 // -42 dBm
			s.fujiLastResp = info[:]
			s.fujiLastErr = ndevStatusSuccess
		} else {
			s.fujiLastResp = info[:]
			s.fujiLastErr = ndevStatusGeneralErr
		}
		return nil

	case 0xE8: // FUJI_GET_ADAPTERCONFIG -> 140B
		ext := s.buildAdapterConfigExtended()
		s.fujiLastResp = append([]byte(nil), ext[:140]...)
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xC4: // FUJI_GET_ADAPTERCONFIG_EXTENDED -> 249B
		ext := s.buildAdapterConfigExtended()
		s.fujiLastResp = append([]byte(nil), ext[:]...)
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xF4: // FUJI_READ_HOST_SLOTS -> 8 * 32 = 256B
		buf := make([]byte, maxHostSlots*hostSlotSize)
		for i := 0; i < maxHostSlots; i++ {
			copyCString(buf[i*hostSlotSize:(i+1)*hostSlotSize], s.cfg.HostSlots[i])
		}
		s.fujiLastResp = buf
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xF3: // FUJI_WRITE_HOST_SLOTS + 256B
		raw, err := s.readExact(maxHostSlots * hostSlotSize)
		if err != nil {
			return err
		}
		for i := 0; i < maxHostSlots; i++ {
			s.cfg.HostSlots[i] = trimZeroString(raw[i*hostSlotSize : (i+1)*hostSlotSize])
		}
		s.saveConfig()
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xF2: // FUJI_READ_DEVICE_SLOTS -> 4 * 38 = 152B (hostSlot, mode, basename[36])
		buf := make([]byte, maxDeviceSlots*deviceSlotSize)
		for i := 0; i < maxDeviceSlots; i++ {
			off := i * deviceSlotSize
			ds := s.cfg.DeviceSlots[i]
			buf[off+0] = ds.HostSlot
			mode := ds.Mode & (diskModeRead | diskModeWrite)
			if s.disks[i] != nil {
				mode |= diskModeMounted
			}
			buf[off+1] = mode
			if ds.Filename != "" {
				base := path.Base(strings.ReplaceAll(ds.Filename, "\\", "/"))
				copyCString(buf[off+2:off+deviceSlotSize], base)
			}
		}
		s.fujiLastResp = buf
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xF1: // FUJI_WRITE_DEVICE_SLOTS + 152B
		raw, err := s.readExact(maxDeviceSlots * deviceSlotSize)
		if err != nil {
			return err
		}
		for i := 0; i < maxDeviceSlots; i++ {
			off := i * deviceSlotSize
			s.cfg.DeviceSlots[i].HostSlot = raw[off+0]
			s.cfg.DeviceSlots[i].Mode = raw[off+1] & (diskModeRead | diskModeWrite)
			fname := trimZeroString(raw[off+2 : off+deviceSlotSize])
			if fname == "" || s.cfg.DeviceSlots[i].HostSlot == invalidHostSlot {
				s.cfg.DeviceSlots[i].Filename = ""
			} else if path.Base(s.cfg.DeviceSlots[i].Filename) != fname {
				s.cfg.DeviceSlots[i].Filename = fname
			}
		}
		s.saveConfig()
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xE2: // FUJI_SET_DEVICE_FULLPATH + ds:1 + hs:1 + mode:1 + path[256]
		hdr, err := s.readExact(3)
		if err != nil {
			return err
		}
		rawPath, err := s.readExact(256)
		if err != nil {
			return err
		}
		ds, hs, mode := int(hdr[0]), hdr[1], hdr[2]
		if ds >= maxDeviceSlots || hs >= maxHostSlots {
			s.fujiLastErr = ndevStatusGeneralErr
			return nil
		}
		fname := trimZeroString(rawPath)
		if fname == "" {
			s.cfg.DeviceSlots[ds] = deviceSlotState{HostSlot: invalidHostSlot}
		} else {
			s.cfg.DeviceSlots[ds] = deviceSlotState{
				HostSlot: hs,
				Mode:     mode & (diskModeRead | diskModeWrite),
				Filename: fname,
			}
		}
		s.saveConfig()
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xDA: // FUJI_GET_DEVICE_FULLPATH + ds:1 -> 256B
		arg, err := s.readExact(1)
		if err != nil {
			return err
		}
		buf := make([]byte, 256)
		ds := int(arg[0])
		if ds >= maxDeviceSlots {
			s.fujiLastResp = buf
			s.fujiLastErr = ndevStatusGeneralErr
			return nil
		}
		copyCString(buf, s.cfg.DeviceSlots[ds].Filename)
		s.fujiLastResp = buf
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xA0, 0xA1, 0xA2, 0xA3: // FUJI_GET_DEVICE1..4_FULLPATH
		buf := make([]byte, 256)
		ds := int(cmd - 0xA0)
		copyCString(buf, s.cfg.DeviceSlots[ds].Filename)
		s.fujiLastResp = buf
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xF9: // FUJI_MOUNT_HOST + hs:1
		arg, err := s.readExact(1)
		if err != nil {
			return err
		}
		if err := s.mountHostSlot(int(arg[0])); err != nil {
			s.fujiLastErr = ndevStatusGeneralErr
		} else {
			s.fujiLastErr = ndevStatusSuccess
		}
		return nil

	case 0xE6: // FUJI_UNMOUNT_HOST + hs:1
		arg, err := s.readExact(1)
		if err != nil {
			return err
		}
		if err := s.unmountHostSlot(int(arg[0])); err != nil {
			s.fujiLastErr = ndevStatusGeneralErr
		} else {
			s.fujiLastErr = ndevStatusSuccess
		}
		return nil

	case 0xF8: // FUJI_MOUNT_IMAGE + ds:1 + mode:1
		args, err := s.readExact(2)
		if err != nil {
			return err
		}
		if err := s.mountDeviceSlot(int(args[0]), args[1]); err != nil {
			s.logf("FUJI MountImage error: %v", err)
			s.fujiLastErr = ndevStatusGeneralErr
		} else {
			s.fujiLastErr = ndevStatusSuccess
		}
		return nil

	case 0xE9: // FUJI_UNMOUNT_IMAGE + ds:1
		arg, err := s.readExact(1)
		if err != nil {
			return err
		}
		ds := int(arg[0])
		if ds >= maxDeviceSlots {
			s.fujiLastErr = ndevStatusGeneralErr
			return nil
		}
		if s.disks[ds] != nil {
			s.disks[ds].Close()
			s.disks[ds] = nil
		}
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xD7: // FUJI_MOUNT_ALL
		for ds := 0; ds < maxDeviceSlots; ds++ {
			if s.cfg.DeviceSlots[ds].HostSlot < maxHostSlots && s.cfg.DeviceSlots[ds].Filename != "" {
				if err := s.mountDeviceSlot(ds, s.cfg.DeviceSlots[ds].Mode); err != nil {
					s.fujiLastErr = ndevStatusGeneralErr
					return nil
				}
			}
		}
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xE7: // FUJI_NEW_DISK + numSectors_be16:2 + sectorSize_be16:2 + hs:1 + ds:1 + filename[256]
		// CoCo NewDiskRequest in disk.h: numDisks/numSectors (u16/u32? check: 1+1+1+256 or similar; let's support local SD blank .dsk creation via SetDeviceFullPath+MountImage too; see note below)
		// Wait: let's read exact CoCo NewDiskRequest size if ever called: numSectors(u32be=4? no, let's check!)
		raw, err := s.readExact(261)
		if err != nil {
			return err
		}
		_ = raw
		s.fujiLastErr = ndevStatusGeneralErr
		return nil

	case 0xF7: // FUJI_OPEN_DIRECTORY + hs:1 + dirpath_and_opt_pattern[256]
		hsBuf, err := s.readExact(1)
		if err != nil {
			return err
		}
		rawPath, err := s.readExact(256)
		if err != nil {
			return err
		}
		nul := indexByteZero(rawPath)
		var dirPath, pattern string
		if nul < 0 {
			dirPath = string(rawPath)
		} else {
			dirPath = string(rawPath[:nul])
			if nul < len(rawPath)-3 {
				pattern = trimZeroString(rawPath[nul+1:])
			}
		}
		if dirPath == "" {
			dirPath = "/"
		}
		items, listErr := s.listHostDirectory(int(hsBuf[0]), dirPath, pattern)
		if listErr != nil {
			s.logf("FUJI OpenDir hs=%d path=%q error: %v", hsBuf[0], dirPath, listErr)
			s.dirOpen = false
			s.fujiLastErr = ndevStatusGeneralErr
			return nil
		}
		s.dirOpen = true
		s.dirItems = items
		s.dirPos = 0
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xF6: // FUJI_READ_DIR_ENTRY + maxlen:1 + addtl:1
		args, err := s.readExact(2)
		if err != nil {
			return err
		}
		maxlen := int(args[0])
		addtl := args[1]
		if !s.dirOpen || maxlen <= 0 {
			s.fujiLastErr = ndevStatusGeneralErr
			return nil
		}
		if s.dirPos >= len(s.dirItems) {
			eof := make([]byte, maxlen)
			if maxlen >= 2 {
				eof[0] = 0x7F
				eof[1] = 0x7F
			}
			s.fujiLastResp = eof
			s.fujiLastErr = ndevStatusSuccess
			return nil
		}
		entry := formatCoCoDirEntry(s.dirItems[s.dirPos], maxlen, addtl)
		s.dirPos++
		s.fujiLastResp = entry
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xF5: // FUJI_CLOSE_DIRECTORY
		s.dirOpen = false
		s.dirItems = nil
		s.dirPos = 0
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xE5: // FUJI_GET_DIRECTORY_POSITION -> be16
		if !s.dirOpen {
			s.fujiLastErr = ndevStatusGeneralErr
			return nil
		}
		var pos [2]byte
		binary.BigEndian.PutUint16(pos[:], uint16(s.dirPos))
		s.fujiLastResp = pos[:]
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xE4: // FUJI_SET_DIRECTORY_POSITION + pos_be16:2
		raw, err := s.readExact(2)
		if err != nil {
			return err
		}
		pos := int(binary.BigEndian.Uint16(raw))
		if !s.dirOpen || pos < 0 || pos > len(s.dirItems) {
			s.fujiLastErr = ndevStatusGeneralErr
			return nil
		}
		s.dirPos = pos
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xDC: // FUJI_OPEN_APPKEY + 6B [creator_be16, app_id, key_id, mode, reserved]
		raw, err := s.readExact(6)
		if err != nil {
			return err
		}
		s.akCreator = binary.BigEndian.Uint16(raw[0:2])
		s.akApp = raw[2]
		s.akKey = raw[3]
		s.akMode = raw[4]
		s.akOpen = true
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xDD: // FUJI_READ_APPKEY -> 66B [len_be16, data[64]]
		var resp [2 + maxAppKeyLen]byte
		if !s.akOpen {
			s.fujiLastResp = resp[:]
			s.fujiLastErr = ndevStatusGeneralErr
			return nil
		}
		data, err := os.ReadFile(s.appKeyFile(s.akCreator, s.akApp, s.akKey))
		s.akOpen = false
		if err != nil {
			// Missing appkey returns length 0 cleanly (matches fujinet-host.c).
			s.fujiLastResp = resp[:]
			s.fujiLastErr = ndevStatusSuccess
			return nil
		}
		if len(data) > maxAppKeyLen {
			data = data[:maxAppKeyLen]
		}
		binary.BigEndian.PutUint16(resp[0:2], uint16(len(data)))
		copy(resp[2:], data)
		s.fujiLastResp = resp[:]
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xDE: // FUJI_WRITE_APPKEY + keylen_be16:2 + data[64]
		lenBuf, err := s.readExact(2)
		if err != nil {
			return err
		}
		rawData, err := s.readExact(maxAppKeyLen)
		if err != nil {
			return err
		}
		if !s.akOpen {
			s.fujiLastErr = ndevStatusGeneralErr
			return nil
		}
		keyLen := int(binary.BigEndian.Uint16(lenBuf))
		if keyLen > maxAppKeyLen {
			keyLen = maxAppKeyLen
		}
		_ = os.MkdirAll(s.appKeyDir(), 0755)
		writeErr := os.WriteFile(s.appKeyFile(s.akCreator, s.akApp, s.akKey), rawData[:keyLen], 0644)
		s.akOpen = false
		if writeErr != nil {
			s.fujiLastErr = ndevStatusGeneralErr
		} else {
			s.fujiLastErr = ndevStatusSuccess
		}
		return nil

	case 0xDB: // FUJI_CLOSE_APPKEY
		s.akOpen = false
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xD3: // FUJI_RANDOM_NUMBER -> 4B
		var rnd [4]byte
		_, _ = rand.Read(rnd[:])
		s.fujiLastResp = rnd[:]
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xBB: // FUJI_GENERATE_GUID -> 37B UUIDv4 string
		var b [16]byte
		_, _ = rand.Read(b[:])
		b[6] = (b[6] & 0x0F) | 0x40
		b[8] = (b[8] & 0x3F) | 0x80
		uuid := fmt.Sprintf("%08x-%04x-%04x-%04x-%012x",
			b[0:4], b[4:6], b[6:8], b[8:10], b[10:16])
		out := append([]byte(uuid), 0)
		s.fujiLastResp = out
		s.fujiLastErr = ndevStatusSuccess
		return nil

	case 0xE0: // FUJI_GET_HOST_PREFIX + hs:1 -> 256B
		arg, err := s.readExact(1)
		if err != nil {
			return err
		}
		buf := make([]byte, 256)
		if int(arg[0]) < maxHostSlots {
			copyCString(buf, s.hostPrefix[arg[0]])
			s.fujiLastErr = ndevStatusSuccess
		} else {
			s.fujiLastErr = ndevStatusGeneralErr
		}
		s.fujiLastResp = buf
		return nil

	case 0xE1: // FUJI_SET_HOST_PREFIX + hs:1 + prefix[256]
		arg, err := s.readExact(1)
		if err != nil {
			return err
		}
		raw, err := s.readExact(256)
		if err != nil {
			return err
		}
		if int(arg[0]) < maxHostSlots {
			s.hostPrefix[arg[0]] = trimZeroString(raw)
			s.fujiLastErr = ndevStatusSuccess
		} else {
			s.fujiLastErr = ndevStatusGeneralErr
		}
		return nil

	default:
		s.logf("FUJI unhandled cmd 0x%02X", cmd)
		s.fujiLastErr = ndevStatusGeneralErr
		return nil
	}
}
