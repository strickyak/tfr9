package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"log"
	// "math/rand"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// --- Protocol OpCodes ---
const (
	OpClear    = 0x01
	OpLine     = 0x02
	OpText     = 0x03
	OpSetColor = 0x04
	OpFillRect = 0x05
	OpBitmap   = 0x06
)

// --- Input Structure (JSON) ---
// This matches the JSON sent by your index.html
type InputEvent struct {
	Type  string `json:"type"`
	Key   string `json:"key,omitempty"`
	X     int    `json:"x,omitempty"`
	Y     int    `json:"y,omitempty"`
	Ctrl  bool   `json:"ctrl,omitempty"`
	Shift bool   `json:"shift,omitempty"`
	Alt   bool   `json:"alt,omitempty"`
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

type WebConsoleConfig struct {
	Bind string
	Key  func(flags uint, s string)
	Move func(x, y int)
	Down func(x, y int)
	Up   func(x, y int)
}

func WebServer(wcc *WebConsoleConfig) {
	http.HandleFunc("/", serveHome(wcc))
	http.HandleFunc("/ws", handleWebSocket(wcc))

	fmt.Printf("Internal web server started at %q\n", wcc.Bind)
	log.Fatal(http.ListenAndServe(wcc.Bind, nil))
}

func serveHome(wcc *WebConsoleConfig) func(w http.ResponseWriter, r *http.Request) {
	return func(w http.ResponseWriter, r *http.Request) {
		http.ServeContent(w, r, "index.html", StartupTime, WebContent)
	}
}

func handleWebSocket(wcc *WebConsoleConfig) func(w http.ResponseWriter, r *http.Request) {
	return func(w http.ResponseWriter, r *http.Request) {
		// Upgrade the HTTP connection to a WebSocket
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			log.Println("Upgrade error:", err)
			return
		}
		defer conn.Close()

		// Mutex is required because we might write to the socket from multiple places
		// (though in this simple example we only write from the main loop)
		var mu sync.Mutex

		// ---------------------------------------------------------
		// CHANNEL 1: INPUT LOOP (Client -> Server)
		// ---------------------------------------------------------
		// We run this in a separate goroutine so it doesn't block the graphics loop.
		go func() {
			for {
				var input InputEvent
				err := conn.ReadJSON(&input)
				if err != nil {
					break
				}

				// LOGIC UPDATE: Handle modifiers and Mouse Moves
				if input.Type == "keydown" {
					// Build a string like "CTRL+ALT+A"
					prefix := ""
					flags := uint(0)
					if input.Ctrl {
						prefix += "CTRL+"
						flags |= 2
					}
					if input.Alt {
						prefix += "ALT+"
						flags |= 4
					}
					if input.Shift {
						prefix += "SHIFT+"
						flags |= 1
					}

					fmt.Printf("KEY: %s%s\n", prefix, input.Key)
					wcc.Key(flags, input.Key)

				} else if input.Type == "mousemove" {
					// fmt.Printf("MOVE: (%d, %d)\n", input.X, input.Y)
					wcc.Move(input.X, input.Y)

				} else if input.Type == "mousedown" {
					// fmt.Printf("DOWN: (%d, %d)\n", input.X, input.Y)
					wcc.Down(input.X, input.Y)

				} else if input.Type == "mouseup" {
					// fmt.Printf("UP: (%d, %d)\n", input.X, input.Y)
					wcc.Up(input.X, input.Y)
				}
			}
		}()

		// ---------------------------------------------------------
		// CHANNEL 2: OUTPUT LOOP (Server -> Client)
		// ---------------------------------------------------------
		// This ticker drives the "Frame Rate" of the emulator (33 us == 30 FPS)
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()

		// Demo State variables
		boxX, boxY := 10, 10
		dx, dy := 2, 2

		for range ticker.C {
			// Update Demo Physics
			boxX += dx
			boxY += dy
			if boxX <= 0 || boxX >= 256-32 {
				dx *= -1
			}
			if boxY <= 0 || boxY >= 200-32 {
				dy *= -1
			}

			// Prepare the Binary Buffer
			buf := new(bytes.Buffer)

			// 1. Draw "UI Controls" (Bottom Area)
			buf.WriteByte(OpSetColor)
			buf.Write([]byte{50, 50, 50}) // Dark Grey

			buf.WriteByte(OpFillRect)
			binary.Write(buf, binary.LittleEndian, uint16(0))   // X
			binary.Write(buf, binary.LittleEndian, uint16(200)) // Y (Start below screen)
			binary.Write(buf, binary.LittleEndian, uint16(300)) // W
			binary.Write(buf, binary.LittleEndian, uint16(50))  // H

			// 2. Clear Screen (Top Area)
			buf.WriteByte(OpSetColor)
			buf.Write([]byte{0, 0, 0}) // Black

			buf.WriteByte(OpFillRect)
			binary.Write(buf, binary.LittleEndian, uint16(0))
			binary.Write(buf, binary.LittleEndian, uint16(0))
			binary.Write(buf, binary.LittleEndian, uint16(256))
			binary.Write(buf, binary.LittleEndian, uint16(200))

			// 3. Draw
			pixelData := GetScreenForWebsocket()
			if pixelData != nil {
				buf.Write(pixelData)
			}

			/*
				patchW, patchH := 32, 32
				buf.WriteByte(OpBitmap)
				binary.Write(buf, binary.LittleEndian, uint16(boxX))
				binary.Write(buf, binary.LittleEndian, uint16(boxY))
				binary.Write(buf, binary.LittleEndian, uint16(patchW))
				binary.Write(buf, binary.LittleEndian, uint16(patchH))

				// Create random noise for pixel data
				pixelData := make([]byte, patchW*patchH*3)
				for i := 0; i < len(pixelData); i++ {
					pixelData[i] = uint8(rand.Intn(255))
				}
				buf.Write(pixelData)
			*/

			// Send the batch
			mu.Lock()
			err := conn.WriteMessage(websocket.BinaryMessage, buf.Bytes())
			mu.Unlock()

			if err != nil {
				log.Println("Write error:", err)
				break
			}
		}
	}
}

func KeystrokeValue(flags uint, s string) byte {
	if len(s) == 1 {
		return s[0]
	} else {
		switch s {
		case "Enter":
			return '\n'
		case "Backspace":
			return 8
		default:
			log.Printf("KV?  [[[%x,%q]]]  ", flags, s)
			return 0
		}
	}
}

var StartupTime = time.Now()

var WebContent = strings.NewReader(strings.ReplaceAll(CONTENT, `"""`, "`"))

const CONTENT = `
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>TFR9 CONSOLE</title>
    <style>
        body { background-color: #222; color: #fff; font-family: monospace; display: flex; flex-direction: column; align-items: center; padding-top: 30px; }
        canvas { 
            background: #000; 
            image-rendering: pixelated; /* Essential for retro look */
            box-shadow: 0 0 20px rgba(0,0,0,0.5);
            /* (NOT YET) cursor: none; */ /* (NOT YET) Hide standard cursor for immersion */
        }
        #status { color: #888; margin-top: 10px; font-size: 0.9em; }
    </style>
</head>
<body>
    <canvas id="screen" width="300" height="250"></canvas>
    <div id="status">Connecting...</div>

    <script>
        const canvas = document.getElementById('screen');
        const ctx = canvas.getContext('2d', { alpha: false }); 
        const statusDiv = document.getElementById('status');
        
        const socket = new WebSocket("ws://" + window.location.host + "/ws");
        socket.binaryType = "arraybuffer";

        socket.onopen = () => { 
            statusDiv.innerText = "System Online"; 
            statusDiv.style.color = "#0f0";
        };

        socket.onclose = () => {
            statusDiv.innerText = "Connection Lost";
            statusDiv.style.color = "#f00";
        };

        // --- PART 1: INCOMING GRAPHICS (Binary) ---
        socket.onmessage = (event) => {
            const buffer = event.data;
            const view = new DataView(buffer);
            let offset = 0;

            while (offset < buffer.byteLength) {
                const opCode = view.getUint8(offset++);

                switch (opCode) {
                    case 0x01: // CLEAR
                        ctx.clearRect(0, 0, canvas.width, canvas.height);
                        break;

                    case 0x02: // LINE
                        const lx1 = view.getUint16(offset, true); offset += 2;
                        const ly1 = view.getUint16(offset, true); offset += 2;
                        const lx2 = view.getUint16(offset, true); offset += 2;
                        const ly2 = view.getUint16(offset, true); offset += 2;
                        ctx.beginPath();
                        ctx.moveTo(lx1, ly1);
                        ctx.lineTo(lx2, ly2);
                        ctx.stroke();
                        break;

                    case 0x03: // TEXT
                        const tx = view.getUint16(offset, true); offset += 2;
                        const ty = view.getUint16(offset, true); offset += 2;
                        const len = view.getUint8(offset++);
                        const textBytes = new Uint8Array(buffer, offset, len);
                        const text = new TextDecoder().decode(textBytes);
                        offset += len;
                        ctx.font = "16px monospace";
                        ctx.fillText(text, tx, ty);
                        break;

                    case 0x04: // SET COLOR
                        const r = view.getUint8(offset++);
                        const g = view.getUint8(offset++);
                        const b = view.getUint8(offset++);
                        const colorString = """rgb(${r},${g},${b})""";
                        ctx.fillStyle = colorString;
                        ctx.strokeStyle = colorString;
                        break;

                    case 0x05: // FILL RECT
                        const rx = view.getUint16(offset, true); offset += 2;
                        const ry = view.getUint16(offset, true); offset += 2;
                        const rw = view.getUint16(offset, true); offset += 2;
                        const rh = view.getUint16(offset, true); offset += 2;
                        ctx.fillRect(rx, ry, rw, rh);
                        break;

                    case 0x06: // BITMAP PATCH
                        const bx = view.getUint16(offset, true); offset += 2;
                        const by = view.getUint16(offset, true); offset += 2;
                        const bw = view.getUint16(offset, true); offset += 2;
                        const bh = view.getUint16(offset, true); offset += 2;

                        const imgData = ctx.createImageData(bw, bh);
                        const d = imgData.data;
                        for (let i = 0; i < bw * bh; i++) {
                            const dataOffset = i * 4;
                            d[dataOffset]     = view.getUint8(offset++); // R
                            d[dataOffset + 1] = view.getUint8(offset++); // G
                            d[dataOffset + 2] = view.getUint8(offset++); // B
                            d[dataOffset + 3] = 255;  // Alpha
                        }
                        ctx.putImageData(imgData, bx, by);
                        break;
                        
                    default:
                        // Sync lost or unknown command
                        offset = buffer.byteLength; 
                }
            }
        };

        // --- PART 2: OUTGOING INPUTS (JSON) ---

        // 1. Keyboard Events
        window.addEventListener('keydown', (e) => {
            if (socket.readyState === WebSocket.OPEN) {
                // Prevent default for arrow keys/space to avoid scrolling
                if(["Space","ArrowUp","ArrowDown","ArrowLeft","ArrowRight"].indexOf(e.code) > -1) {
                    e.preventDefault();
                }

                // SEND MODIFIERS along with the key
                socket.send(JSON.stringify({
                    type: "keydown",
                    key: e.key,
                    ctrl: e.ctrlKey,   // true if Ctrl is held
                    shift: e.shiftKey, // true if Shift is held
                    alt: e.altKey      // true if Alt is held
                }));
            }
        });

        // 2. Mouse Movement
        canvas.addEventListener('mousemove', (e) => {
            if (socket.readyState === WebSocket.OPEN) {
                const rect = canvas.getBoundingClientRect();
                socket.send(JSON.stringify({
                    type: "mousemove",
                    x: Math.floor(e.clientX - rect.left),
                    y: Math.floor(e.clientY - rect.top)
                }));
            }
        });

        // 3. Mouse Clicks
        canvas.addEventListener('mousedown', (e) => {
             if (socket.readyState === WebSocket.OPEN) {
                socket.send(JSON.stringify({
                    type: "mousedown",
                    key: "mouse_left",
                    x: e.offsetX, // Simpler way to get coordinates relative to element
                    y: e.offsetY
                }));
            }
        });
        canvas.addEventListener('mouseup', (e) => {
             if (socket.readyState === WebSocket.OPEN) {
                socket.send(JSON.stringify({
                    type: "mouseup",
                    key: "mouse_left",
                    x: e.offsetX, // Simpler way to get coordinates relative to element
                    y: e.offsetY
                }));
            }
        });

    </script>
</body>
</html>
`
