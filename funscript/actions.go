package funscript

// SaveActions schreibt eine bearbeitete Punktliste (z.B. aus dem
// Kurven-Editor) in ein bestehendes .funscript zurück - nach demselben
// Prinzip wie SaveOMarkers: nur der "actions"-Schlüssel wird ersetzt, alles
// andere (metadata, auch von anderen Werkzeugen dort abgelegte unbekannte
// Felder) bleibt Byte für Byte erhalten statt über den typisierten
// Script-Typ neu serialisiert zu werden.
//
// Positionswerte außerhalb 0-100 werden geklemmt (wie beim Einlesen in
// Parse), keine negative Zeit erlaubt, und es wird nach der Zeit sortiert -
// ein Ziehen im Editor kann die Reihenfolge während der Bewegung kurzzeitig
// durcheinanderbringen, das Ergebnis muss trotzdem eine gültige, aufsteigend
// sortierte Punktliste sein. Eine leere Liste wird abgelehnt: ein Skript
// ohne jeden Punkt lässt sich anschließend nicht mehr laden (siehe Parse).
func SaveActions(path string, actions []Action) error {
	return SaveAxisActions(path, AxisGeneral, actions)
}
