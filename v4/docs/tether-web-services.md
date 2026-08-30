# Tether Web Services API

The tether program includes an internal HTTP web server listening on port `localhost:8080` (or the configured bind address). In addition to serving the interactive web console canvas and WebSocket streaming, it provides endpoints for inspecting the live shadow RAM maintained by the tether.

---

## Endpoints

### 1. `/ram` - Inspect Shadow RAM

Returns the contents of the 6809 shadow RAM (`the_ram`) tracked in real time from CPU write cycle records sent over USB.

#### HTTP Method
`GET`

#### Query Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `addr` or `offset` | integer / hex | `0` | Starting memory address. Supports decimal (e.g. `1024`) or hexadecimal (`0x0400`, `$0400`, `0400`). |
| `len` or `size` or `length` | integer / hex | to end of RAM | Number of bytes to retrieve. |
| `format` | string | `hd` | Output format: `hd` (default hexdump), `bin` (raw binary), `hex` (hexadecimal string). |

---

### Output Formats

#### 1. Default (`format=hd` / text hexdump)
Returns a human-readable `text/plain` formatted hexdump similar to `hd` (`hexdump -C`), with **two side-by-side character columns**:
1. **Standard ASCII**: Printable ASCII characters (`0x20..0x7E`), non-printable as `.`.
2. **VDG 64-Character Set**: Motorola MC6847 VDG screen codes (e.g., `0x00` = `@`, `0x01..0x1A` = `A..Z`, `0x20` = space, `0x40..0x5A` = inverted `A..Z`).

**Example Request:**
```http
GET /ram?addr=0x0400&len=64
```

**Example Response (`Content-Type: text/plain; charset=utf-8`):**
```text
00000400  4f 4b 0d 60 60 60 60 60  60 60 60 60 60 60 60 60  |OK.`````````````| |OK.             |
00000410  60 60 60 60 60 60 60 60  60 60 60 60 60 60 60 60  |````````````````| |                |
00000420  31 30 20 50 52 49 4e 54  20 22 48 45 4c 4c 4f 22  |10 PRINT "HELLO"| |10 PRINT "HELLO"|
00000430  60 60 60 60 60 60 60 60  60 60 60 60 60 60 60 60  |````````````````| |                |
```

---

#### 2. Raw Binary (`format=bin` or `/ram.bin`)
Returns raw binary bytes.

**Example Request:**
```http
GET /ram?addr=0x0400&len=512&format=bin
GET /ram.bin?addr=0x0400&len=512
```

**Response:**
* `Content-Type: application/octet-stream`
* `Content-Length: <length>`

---

#### 3. Programmatic Hex String (`format=hex` or `/ram.hex`)
Returns a continuous lowercase hexadecimal string followed by a newline, suitable for programmatic parsing without binary encoding issues.

**Example Request:**
```http
GET /ram?addr=0x0400&len=16&format=hex
GET /ram.hex?addr=0x0400&len=16
```

**Example Response (`Content-Type: text/plain; charset=utf-8`):**
```text
4f4b0d60606060606060606060606060
```

---

## Common Inspection Memory Ranges (CoCo 1 & 2)

| Address Range | Size | Description |
|---|---|---|
| `$0400..$05FF` | 512 bytes | 32×16 VDG Text Screen RAM (`addr=0x0400&len=512`) |
| `$0600..$06FF` | 256 bytes | Disk BASIC Sector I/O Buffer (`addr=0x0600&len=256`) |
| `$0980..$098F` | 16 bytes | DSKCON internal variables (`$0984` = `DCSTA` status, `$0982` = `NMIFLG`) |
| `$0019..$0020` | 8 bytes | BASIC program & variable pointers (`TXTTAB`, `VARTAB`, `ARYTAB`) |
