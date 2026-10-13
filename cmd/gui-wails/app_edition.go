package main

import (
	"errors"

	"github.com/funfunpayer/SamNPlayer/edition"
)

// errSuiteOnly is returned by AI-training and learning bindings in the
// Free build. The bindings stay so the Wails JS API is the same in both
// editions; the Free GUI never shows the controls that call them.
var errSuiteOnly = errors.New("not available in this edition — AI training and learning tools are part of the internal suite build")

// IsSuiteBuild tells the GUI which edition is running (see package edition).
func (a *App) IsSuiteBuild() bool { return edition.Suite() }

func requireSuite() error {
	if edition.Suite() {
		return nil
	}
	return errSuiteOnly
}
