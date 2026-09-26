package bootmenu

import (
	"bufio"
	"io"
	"regexp"
	"strings"
)

type Widget struct {
	ID        string
	Row       int
	Col       int
	Width     int
	Type      string
	FieldName string
}

type Constraint struct {
	Type      string
	WidgetIDs []string
}

type Screen struct {
	Name        string
	Lines       []string
	Widgets     []*Widget
	Constraints []Constraint
}

type MenuConfig struct {
	Screens []*Screen
}

var widgetPattern = regexp.MustCompile(`\[([^\]]+)\]`)

func Parse(r io.Reader) (*MenuConfig, error) {
	scanner := bufio.NewScanner(r)
	config := &MenuConfig{}
	
	var currentScreen *Screen
	state := 0 // 0: outside, 1: reading UI, 2: reading definitions

	for scanner.Scan() {
		line := scanner.Text()
		
		// Ignore comments
		if strings.HasPrefix(strings.TrimSpace(line), "#") {
			continue
		}

		// Check for menu start
		if strings.HasPrefix(line, "menu ") {
			parts := strings.Fields(line)
			if len(parts) >= 2 {
				currentScreen = &Screen{
					Name: parts[1],
				}
				config.Screens = append(config.Screens, currentScreen)
				state = 0
			}
			continue
		}

		// Check for end of menu
		if strings.TrimSpace(line) == ";" {
			currentScreen = nil
			state = 0
			continue
		}

		if currentScreen == nil {
			continue
		}

		if strings.HasPrefix(line, "||||") {
			if state == 0 {
				state = 1
			} else if state == 1 {
				state = 2
			}
			continue
		}

		if state == 1 {
			// Read UI line
			text := line
			if len(line) >= 4 {
				text = line[4:]
			}
			
			row := len(currentScreen.Lines)
			currentScreen.Lines = append(currentScreen.Lines, text)

			matches := widgetPattern.FindAllStringSubmatchIndex(text, -1)
			for _, match := range matches {
				start := match[0]
				end := match[1]
				innerStart := match[2]
				innerEnd := match[3]
				
				id := strings.TrimSpace(text[innerStart:innerEnd])
				widget := &Widget{
					ID:    id,
					Row:   row,
					Col:   start,
					Width: end - start,
				}
				currentScreen.Widgets = append(currentScreen.Widgets, widget)
			}
		} else if state == 2 {
			// Read field definition or constraint
			parts := strings.Fields(line)
			if len(parts) == 0 {
				continue
			}

			cmd := parts[0]
			if cmd == "atmostone" || cmd == "exactlyone" {
				// e.g. atmostone 1, 3
				var ids []string
				joined := strings.Join(parts[1:], "")
				for _, id := range strings.Split(joined, ",") {
					id = strings.TrimSpace(id)
					if id != "" {
						ids = append(ids, id)
					}
				}
				currentScreen.Constraints = append(currentScreen.Constraints, Constraint{
					Type:      cmd,
					WidgetIDs: ids,
				})
			} else if len(parts) >= 4 {
				// e.g. check 1 = smallram OR check 3 name bigram
				wType := parts[0]
				wID := parts[1]
				fieldName := strings.Join(parts[3:], " ")
				
				for _, w := range currentScreen.Widgets {
					if w.ID == wID {
						w.Type = wType
						w.FieldName = fieldName
						break
					}
				}
			}
		}
	}

	return config, scanner.Err()
}
