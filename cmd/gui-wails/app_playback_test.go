package main

import (
	"sync"
	"testing"

	"github.com/funfunpayer/SamNPlayer/device"
	"github.com/funfunpayer/SamNPlayer/player"
)

// Reproduziert eine echte Race: ReportVideoPosition (aus dem Frontend bei
// jedem "timeupdate" des Videos, ~4x/s) und StopPlayback (beim Stop-Klick)
// griffen beide unsynchronisiert auf a.videoPositionCh zu. Landete
// StopPlaybacks close() zwischen ReportVideoPositions nil-Prüfung und
// seinem send, panicte das mit "send on closed channel" - reproduziert vor
// dem Fix zuverlässig sowohl unter go test -race als auch (seltener, aber
// real) ganz ohne -race.
func TestReportVideoPositionStopPlaybackRace(t *testing.T) {
	a := NewApp()
	a.videoPositionCh = make(chan int64, 4)

	var wg sync.WaitGroup
	wg.Add(2)
	go func() {
		defer wg.Done()
		for i := 0; i < 2000; i++ {
			a.ReportVideoPosition(int64(i))
		}
	}()
	go func() {
		defer wg.Done()
		a.StopPlayback()
	}()
	wg.Wait()
}

// Reproduziert eine weitere echte Race, gefunden bei einer Auditrunde
// (nicht durch einen konkreten Fehlerbericht): StartPlayback setzte
// a.activeDevice/a.activePlayer bisher unguarded, während TriggerExtendedO
// (Wails-Bindung, vom Frontend beim "Extended-O auslösen"-Klick oder der
// O-Taste aufgerufen) a.activePlayer ebenso unguarded las - ein
// Wiedergabe-Start und ein kurz danach ausgelöstes Extended-O konnten
// gleichzeitig auf dasselbe Feld zugreifen. Beide sind jetzt unter stateMu
// wie jedes andere geteilte App-Feld.
func TestActivePlayerStartAndTriggerExtendedORace(t *testing.T) {
	a := NewApp()

	var wg sync.WaitGroup
	wg.Add(2)
	go func() {
		defer wg.Done()
		for i := 0; i < 500; i++ {
			p := player.New(device.NewMock(false))
			a.stateMu.Lock()
			a.activeDevice = p.Device
			a.activePlayer = p
			a.stateMu.Unlock()
		}
	}()
	go func() {
		defer wg.Done()
		for i := 0; i < 2000; i++ {
			a.TriggerExtendedO(0.1, 1, 100)
		}
	}()
	wg.Wait()
}
