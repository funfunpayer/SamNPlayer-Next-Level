package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/funfunpayer/SamNPlayer/player"
)

// Custom training scripts (see docs/TRAINING_MODE_RESEARCH.md's "Follow-up
// design: multi-phase, per-channel scripts") - the GUI script editor lets
// a user build their own TrainingScript instead of only picking one of
// player.BuiltinTrainingScripts(). Saved as one JSON file per script,
// same convention as messwerte.jsonl/golden_clip_history.jsonl etc.
// (os.UserConfigDir()/SamNPlayer/<name>), just a directory instead of one
// file since each script is its own document a user creates/edits/deletes
// independently.

func customScriptsDir() (string, error) {
	dir, err := os.UserConfigDir()
	if err != nil {
		return "", fmt.Errorf("training scripts: config directory not available: %w", err)
	}
	return filepath.Join(dir, "SamNPlayer", "training_scripts"), nil
}

var unsafeScriptNameChars = regexp.MustCompile(`[^a-z0-9-]+`)

// scriptFileSlug turns a user-typed script name into a safe filename
// component. Names come straight from a GUI text field, so this has to
// defend against path traversal ("../../etc/passwd") and empty/
// whitespace-only input, not just look tidy.
func scriptFileSlug(name string) (string, error) {
	slug := unsafeScriptNameChars.ReplaceAllString(strings.ToLower(strings.TrimSpace(name)), "-")
	slug = strings.Trim(slug, "-")
	if slug == "" {
		return "", fmt.Errorf("script name must contain at least one letter or number")
	}
	return slug, nil
}

// reservedForBuiltins rejects a custom script name that collides with a
// built-in one - StartTraining/TrainingScriptPreview look built-ins up
// first, so a same-named custom script would silently become
// unreachable instead of shadowing or erroring clearly.
func isBuiltinScriptName(name string) bool {
	_, ok := player.BuiltinTrainingScript(name)
	return ok
}

// SaveTrainingScript validates, normalizes (see
// player.NormalizeTrainingScript) and writes a user-built script to its
// own JSON file, keyed by a slug of its name - overwrites an existing
// file with the same slug, i.e. saving again with the same name edits
// it in place.
func (a *App) SaveTrainingScript(script player.TrainingScript) (TrainingScriptInfo, error) {
	script.Name = strings.TrimSpace(script.Name)
	if script.Name == "" {
		return TrainingScriptInfo{}, fmt.Errorf("script needs a name")
	}
	if isBuiltinScriptName(script.Name) {
		return TrainingScriptInfo{}, fmt.Errorf("%q is a built-in script name - pick a different name", script.Name)
	}
	script = player.NormalizeTrainingScript(script)
	if err := player.ValidateTrainingScript(script); err != nil {
		return TrainingScriptInfo{}, err
	}

	slug, err := scriptFileSlug(script.Name)
	if err != nil {
		return TrainingScriptInfo{}, err
	}
	dir, err := customScriptsDir()
	if err != nil {
		return TrainingScriptInfo{}, err
	}
	if err := os.MkdirAll(dir, 0755); err != nil {
		return TrainingScriptInfo{}, err
	}

	// scriptFileSlug strips everything but [a-z0-9-], so two DIFFERENT
	// names (e.g. "My Routine!" and "My Routine?") can reduce to the same
	// slug and thus the same file. Without this check, saving the second
	// one would silently overwrite the first - same file name, but
	// SaveTrainingScript never even loads the old content to notice the
	// name changed. Re-saving under the SAME name (editing a script in
	// place) must keep working, so only reject when a script already
	// lives at that path under a DIFFERENT name.
	scriptPath := filepath.Join(dir, slug+".json")
	if existing, readErr := readScriptFile(scriptPath); readErr == nil && existing.Name != script.Name {
		return TrainingScriptInfo{}, fmt.Errorf(
			"script name %q reduces to the same file name (%q) as the already-saved script %q - pick a name that differs in letters or digits, not just spelling/punctuation/case",
			script.Name, slug, existing.Name)
	}

	b, err := json.MarshalIndent(script, "", "  ")
	if err != nil {
		return TrainingScriptInfo{}, err
	}
	if err := os.WriteFile(scriptPath, b, 0644); err != nil {
		return TrainingScriptInfo{}, err
	}
	return TrainingScriptInfo{Name: script.Name, Description: script.Description, Custom: true}, nil
}

// listCustomTrainingScripts reads every saved custom script (full
// structs, not just the GUI-dropdown summary) - shared by
// ListTrainingScripts (summary) and loadAnyTrainingScript (full struct,
// for StartTraining/editing/preview).
func listCustomTrainingScripts() ([]player.TrainingScript, error) {
	dir, err := customScriptsDir()
	if err != nil {
		return nil, err
	}
	entries, err := os.ReadDir(dir)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	var scripts []player.TrainingScript
	for _, e := range entries {
		if e.IsDir() || !strings.HasSuffix(e.Name(), ".json") {
			continue
		}
		script, err := readScriptFile(filepath.Join(dir, e.Name()))
		if err != nil {
			continue // eine unlesbare/beschädigte Datei darf die anderen nicht mitreißen
		}
		scripts = append(scripts, script)
	}
	sort.Slice(scripts, func(i, j int) bool { return scripts[i].Name < scripts[j].Name })
	return scripts, nil
}

// readScriptFile reads and decodes one saved custom script's JSON file.
func readScriptFile(path string) (player.TrainingScript, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return player.TrainingScript{}, err
	}
	var script player.TrainingScript
	if err := json.Unmarshal(b, &script); err != nil {
		return player.TrainingScript{}, err
	}
	return script, nil
}

// loadAnyTrainingScript looks a script up by name across BOTH sources -
// built-in first (so a same-named custom script can never shadow one,
// matching the rejection in SaveTrainingScript), then custom. Single
// lookup point for StartTraining, TrainingScriptPreview and the editor's
// "load for editing" action.
func loadAnyTrainingScript(name string) (player.TrainingScript, error) {
	if script, ok := player.BuiltinTrainingScript(name); ok {
		return script, nil
	}
	scripts, err := listCustomTrainingScripts()
	if err != nil {
		return player.TrainingScript{}, err
	}
	for _, s := range scripts {
		if s.Name == name {
			return s, nil
		}
	}
	return player.TrainingScript{}, fmt.Errorf("unknown training script %q", name)
}

// DeleteTrainingScript removes a saved custom script. Rejects built-in
// names up front instead of silently no-oping, so a typo in the GUI
// doesn't look like a successful delete.
func (a *App) DeleteTrainingScript(name string) error {
	if isBuiltinScriptName(name) {
		return fmt.Errorf("%q is a built-in script and cannot be deleted", name)
	}
	slug, err := scriptFileSlug(name)
	if err != nil {
		return err
	}
	dir, err := customScriptsDir()
	if err != nil {
		return err
	}
	path := filepath.Join(dir, slug+".json")
	if err := os.Remove(path); err != nil {
		if os.IsNotExist(err) {
			return fmt.Errorf("no custom script named %q", name)
		}
		return err
	}
	return nil
}

// LoadTrainingScriptForEditing returns a custom script's full, editable
// structure - the editor loads this to populate its form. Built-in
// scripts are not editable in place (SaveTrainingScript rejects their
// names); the editor can still load one as a starting point for a new,
// differently-named script by asking for TrainingScriptPreview's source
// instead, but that's a frontend-side "duplicate" affordance, not a
// separate Go method.
func (a *App) LoadTrainingScriptForEditing(name string) (player.TrainingScript, error) {
	if isBuiltinScriptName(name) {
		return player.TrainingScript{}, fmt.Errorf("%q is built-in and read-only - duplicate it to edit", name)
	}
	scripts, err := listCustomTrainingScripts()
	if err != nil {
		return player.TrainingScript{}, err
	}
	for _, s := range scripts {
		if s.Name == name {
			return s, nil
		}
	}
	return player.TrainingScript{}, fmt.Errorf("no custom script named %q", name)
}

// PreviewTrainingScriptDraft renders a script the editor is currently
// building - normalizes and validates it first (the same checks
// SaveTrainingScript runs), so the preview also doubles as the editor's
// "is this valid yet" feedback before the user tries to save or start it.
func (a *App) PreviewTrainingScriptDraft(script player.TrainingScript) (TrainingScriptPreviewResult, error) {
	script = player.NormalizeTrainingScript(script)
	if err := player.ValidateTrainingScript(script); err != nil {
		return TrainingScriptPreviewResult{}, err
	}
	return buildScriptPreview(script), nil
}
