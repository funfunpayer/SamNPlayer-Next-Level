package logging

import (
	"fmt"
	"strings"
	"sync"
	"time"
)

// Entry is one line in the in-memory log ring (for the GUI Log tab).
type Entry struct {
	Time    time.Time `json:"time"`
	Level   string    `json:"level"`
	Message string    `json:"message"`
}

const ringCap = 800

var (
	ringMu   sync.Mutex
	ring     = make([]Entry, 0, ringCap)
	liveHook func(Entry)
)

// SetLiveHook registers an optional callback for every new ring entry
// (GUI: EventsEmit). Pass nil to clear.
func SetLiveHook(fn func(Entry)) {
	ringMu.Lock()
	liveHook = fn
	ringMu.Unlock()
}

// Recent returns a copy of the ring (oldest first).
func Recent() []Entry {
	ringMu.Lock()
	defer ringMu.Unlock()
	out := make([]Entry, len(ring))
	copy(out, ring)
	return out
}

// ClearRecent empties the in-memory ring (file log untouched).
func ClearRecent() {
	ringMu.Lock()
	ring = ring[:0]
	ringMu.Unlock()
}

func appendRing(level, msg string, args ...any) {
	line := formatMsg(msg, args...)
	e := Entry{Time: time.Now(), Level: level, Message: line}
	ringMu.Lock()
	if len(ring) >= ringCap {
		copy(ring, ring[len(ring)-ringCap+1:])
		ring = ring[:ringCap-1]
	}
	ring = append(ring, e)
	hook := liveHook
	ringMu.Unlock()
	if hook != nil {
		hook(e)
	}
}

func formatMsg(msg string, args ...any) string {
	if len(args) == 0 {
		return msg
	}
	var b strings.Builder
	b.WriteString(msg)
	for i := 0; i+1 < len(args); i += 2 {
		b.WriteString(" ")
		b.WriteString(fmt.Sprint(args[i]))
		b.WriteString("=")
		b.WriteString(fmt.Sprint(args[i+1]))
	}
	if len(args)%2 == 1 {
		b.WriteString(" ")
		b.WriteString(fmt.Sprint(args[len(args)-1]))
	}
	return b.String()
}
