package main

import (
	"flag"
	"strconv"
	"strings"
)

// splitCLIArgs separates positional paths from flag args so paths may appear
// before or after options. Unlike a naive "next token after -X is a value
// unless it starts with -", negative numbers (e.g. --max-lag-ms -500) are
// treated as values. Bare "--" ends option scanning.
func splitCLIArgs(fs *flag.FlagSet, args []string) (paths, flagArgs []string) {
	for i := 0; i < len(args); i++ {
		a := args[i]
		if a == "--" {
			paths = append(paths, args[i+1:]...)
			return paths, flagArgs
		}
		if strings.HasPrefix(a, "-") && a != "-" {
			flagArgs = append(flagArgs, a)
			if strings.Contains(a, "=") || isBoolFlag(fs, a) {
				continue
			}
			if i+1 >= len(args) {
				continue
			}
			next := args[i+1]
			if isFlagValueToken(next) {
				flagArgs = append(flagArgs, next)
				i++
			}
			continue
		}
		paths = append(paths, a)
	}
	return paths, flagArgs
}

// isBoolFlag uses the standard flag package's optional IsBoolFlag contract.
// Boolean options do not consume the following token, so a positional path may
// immediately follow one (for example: sam --thin clip.funscript).
func isBoolFlag(fs *flag.FlagSet, arg string) bool {
	if fs == nil {
		return false
	}
	name := strings.TrimLeft(arg, "-")
	if i := strings.IndexByte(name, '='); i >= 0 {
		name = name[:i]
	}
	f := fs.Lookup(name)
	if f == nil {
		return false
	}
	type boolFlag interface {
		IsBoolFlag() bool
	}
	b, ok := f.Value.(boolFlag)
	return ok && b.IsBoolFlag()
}

// isFlagValueToken reports whether s should be consumed as a flag value
// rather than as another option or a path. Negative integers count as values.
func isFlagValueToken(s string) bool {
	if s == "" || s == "-" || s == "--" {
		return false
	}
	if strings.HasPrefix(s, "--") {
		return false // another long option
	}
	if strings.HasPrefix(s, "-") {
		// short option OR negative number
		if _, err := strconv.ParseInt(s, 10, 64); err == nil {
			return true
		}
		if _, err := strconv.ParseFloat(s, 64); err == nil {
			return true
		}
		return false
	}
	return true
}
