// Package edition is the build switch between the Free build and the
// internal suite build (Owner 11 Oct 2026).
//
// The Free build (default, what releases ship) runs SamNPlayer with all
// player, generator and AI-assist features. AI training and learning tools —
// AI Train, the golden-clip benchmark, learning-data export, model training,
// candidate review for datasets — are only in the suite build:
//
//	go build -tags suite ./cmd/cli
//	wails build -tags "suite ..."
package edition

// Suite reports whether this binary is the internal suite build.
func Suite() bool { return suite }

// Name is "suite" or "free" (logs, About).
func Name() string {
	if suite {
		return "suite"
	}
	return "free"
}
