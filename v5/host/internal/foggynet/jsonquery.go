package foggynet

import (
	"bytes"
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
)

// ParseJSON decodes a JSON document preserving exact numeric string representations via json.Number.
func ParseJSON(raw []byte) (any, error) {
	trimmed := bytes.TrimSpace(raw)
	// Strip UTF-8 BOM if present.
	trimmed = bytes.TrimPrefix(trimmed, []byte{0xEF, 0xBB, 0xBF})
	dec := json.NewDecoder(bytes.NewReader(trimmed))
	dec.UseNumber()
	var doc any
	if err := dec.Decode(&doc); err != nil {
		return nil, err
	}
	return doc, nil
}

// QueryJSON evaluates a FujiNet JSON path query (e.g. "/current/time" or "/results/0/latitude")
// against a parsed JSON value and returns the scalar string representation (without trailing EOL).
func QueryJSON(doc any, rawQuery string) string {
	q := strings.TrimSpace(rawQuery)
	q = strings.Trim(q, "/")
	cur := doc
	if q != "" {
		parts := strings.Split(q, "/")
		for _, part := range parts {
			if cur == nil {
				return ""
			}
			switch node := cur.(type) {
			case map[string]any:
				v, ok := node[part]
				if !ok {
					// Case-insensitive fallback if exact key not present.
					found := false
					for k, kv := range node {
						if strings.EqualFold(k, part) {
							v = kv
							found = true
							break
						}
					}
					if !found {
						return ""
					}
				}
				cur = v

			case []any:
				idx, err := strconv.Atoi(part)
				if err != nil || idx < 0 || idx >= len(node) {
					return ""
				}
				cur = node[idx]

			default:
				return ""
			}
		}
	}

	return formatJSONScalar(cur)
}

func formatJSONScalar(v any) string {
	if v == nil {
		return ""
	}
	switch val := v.(type) {
	case string:
		return val
	case json.Number:
		return val.String()
	case bool:
		if val {
			return "true"
		}
		return "false"
	case float64:
		return strconv.FormatFloat(val, 'f', -1, 64)
	case map[string]any, []any:
		// FujiNet JSON query returns empty string when targeting a non-scalar container,
		// matching fujinet-host.c (`print(v if not isinstance(v,(dict,list)) else "")`).
		return ""
	default:
		return fmt.Sprint(val)
	}
}
