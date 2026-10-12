//go:build windows

package generator

import "syscall"

// hiddenSysProcAttr unterdrückt das Konsolenfenster, das Windows sonst für
// jeden gestarteten Python-Unterprozess kurz aufblitzen lässt - spürbar
// störend, weil praktisch jede Aktion im Generator-Tab (Vorschau laden,
// Abhängigkeiten prüfen, Region suchen, Skript erzeugen, ...) einen eigenen
// Python-Prozess startet. Ohne HideWindow sieht der Nutzer bei jedem dieser
// Schritte ein kurz aufblitzendes schwarzes Fenster.
func hiddenSysProcAttr() *syscall.SysProcAttr {
	return &syscall.SysProcAttr{HideWindow: true}
}
