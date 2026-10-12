package device

import (
	"bytes"
	"testing"
)

// Diese Testfälle sind 1:1 aus dem offiziellen Buttplug-Rust-Quellcode
// abgeleitet (svakom_sam2.rs + svakom-sam2.yml), nicht aus eigener
// Interpretation. Sie dienen als Regressionsschutz gegen versehentliche
// Abweichungen vom verifizierten Protokoll.
func TestSamNeo2Protocol_EncodeVibration(t *testing.T) {
	p := SamNeo2Protocol{}
	cases := []struct {
		name      string
		intensity float64
		want      []byte
	}{
		{"aus", 0.0, []byte{0x55, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{"volle Kraft (10)", 1.0, []byte{0x55, 0x03, 0x00, 0x00, 0x05, 0x0A, 0x00}},
		{"70% -> speed 7", 0.7, []byte{0x55, 0x03, 0x00, 0x00, 0x05, 0x07, 0x00}},
		{"minimal ungleich 0 -> speed 1", 0.06, []byte{0x55, 0x03, 0x00, 0x00, 0x05, 0x01, 0x00}},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			got := p.EncodeVibration(c.intensity)
			if !bytes.Equal(got, c.want) {
				t.Errorf("EncodeVibration(%v) = % x, want % x", c.intensity, got, c.want)
			}
		})
	}
}

func TestSamNeo2Protocol_EncodeSuction(t *testing.T) {
	p := SamNeo2Protocol{}
	cases := []struct {
		name      string
		intensity float64
		want      []byte
	}{
		{"aus", 0.0, []byte{0x55, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00}},
		{"volle Kraft (5)", 1.0, []byte{0x55, 0x09, 0x00, 0x00, 0x01, 0x05, 0x00}},
		{"60% -> level 3", 0.6, []byte{0x55, 0x09, 0x00, 0x00, 0x01, 0x03, 0x00}},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			got := p.EncodeSuction(c.intensity)
			if !bytes.Equal(got, c.want) {
				t.Errorf("EncodeSuction(%v) = % x, want % x", c.intensity, got, c.want)
			}
		})
	}
}

func TestSamNeo2Protocol_UUIDsAndWriteMode(t *testing.T) {
	p := SamNeo2Protocol{}
	if p.ServiceUUID() != "0000ffe0-0000-1000-8000-00805f9b34fb" {
		t.Errorf("unerwartete Service-UUID: %s", p.ServiceUUID())
	}
	if p.CharacteristicUUID() != "0000ffe1-0000-1000-8000-00805f9b34fb" {
		t.Errorf("unerwartete Characteristic-UUID: %s", p.CharacteristicUUID())
	}
	if !p.WriteWithResponse() {
		t.Error("Sam Neo 2 erfordert laut Quellcode Write-With-Response, WriteWithResponse() muss true liefern")
	}
}

// Die Rohwert-Kodierung darf NICHT quantisieren - sie existiert genau
// dafür, Werte jenseits der aus svakom-sam2.yml übernommenen Bereiche
// (0-10 Vibration, 0-5 Sog) am echten Gerät ausprobieren zu können.
func TestSamNeo2RawEncodingDoesNotQuantize(t *testing.T) {
	p := SamNeo2Protocol{}
	for _, speed := range []byte{0, 1, 3, 4, 10, 11, 50, 200, 255} {
		packet := p.EncodeVibrationRaw(speed)
		if len(packet) != 7 {
			t.Fatalf("Vibrationspaket hat %d statt 7 Bytes", len(packet))
		}
		if packet[5] != speed {
			t.Errorf("Rohwert %d wurde zu %d verändert", speed, packet[5])
		}
		wantPattern := byte(0x05)
		if speed == 0 {
			wantPattern = 0x00
		}
		if packet[4] != wantPattern {
			t.Errorf("speed=%d: pattern %#x, erwartet %#x", speed, packet[4], wantPattern)
		}
		if packet[0] != 0x55 || packet[1] != 0x03 {
			t.Errorf("speed=%d: falscher Kopf % x", speed, packet[:2])
		}
	}

	for _, level := range []byte{0, 1, 5, 6, 99, 255} {
		packet := p.EncodeSuctionRaw(level)
		if packet[5] != level {
			t.Errorf("Sog-Rohwert %d wurde zu %d verändert", level, packet[5])
		}
		wantFlag := byte(0x01)
		if level == 0 {
			wantFlag = 0x00
		}
		if packet[4] != wantFlag {
			t.Errorf("level=%d: flag %#x, erwartet %#x", level, packet[4], wantFlag)
		}
		if packet[1] != 0x09 {
			t.Errorf("level=%d: falscher Kommandotyp %#x", level, packet[1])
		}
	}
}

// Gegenprobe: der normale Weg quantisiert weiterhin wie bisher.
func TestSamNeo2NormalEncodingStillQuantizes(t *testing.T) {
	p := SamNeo2Protocol{}
	cases := []struct {
		intensity float64
		wantSpeed byte
	}{{0, 0}, {0.05, 1}, {0.5, 5}, {0.94, 9}, {1.0, 10}}
	for _, tc := range cases {
		if got := p.EncodeVibration(tc.intensity)[5]; got != tc.wantSpeed {
			t.Errorf("EncodeVibration(%.2f) = %d, erwartet %d", tc.intensity, got, tc.wantSpeed)
		}
	}
}

// Das Gerät kennt nur ganzzahlige Stufen. Aufeinanderfolgende Intensitäten
// landen deshalb häufig auf derselben Stufe - und ein Schreibvorgang, der
// am Gerät nichts ändert, kostet trotzdem einen vollen Roundtrip mit
// Bestätigung. Bei zwei Kanälen alle 50ms wären das bis zu 40 pro Sekunde.
//
// Der Test hält fest, wie viele davon überhaupt unterschiedliche Pakete
// ergeben - das ist die Obergrenze dessen, was das Zusammenfassen sparen
// kann.
func TestEncodingCollapsesNearbyIntensities(t *testing.T) {
	p := SamNeo2Protocol{}

	// Eine typische Rampe, wie sie der Trainingsmodus fährt: 100 Schritte
	// von 0 auf 1.
	distinct := map[string]bool{}
	for i := 0; i <= 100; i++ {
		packet := p.EncodeVibration(float64(i) / 100.0)
		distinct[string(packet)] = true
	}
	if len(distinct) > 11 {
		t.Errorf("100 Rampenschritte ergeben %d verschiedene Pakete, "+
			"erwartet höchstens 11 (Stufen 0-10)", len(distinct))
	}
	if len(distinct) < 5 {
		t.Errorf("nur %d verschiedene Pakete - die Rampe wäre zu grob", len(distinct))
	}

	// Zwei benachbarte Intensitäten auf derselben Stufe MÜSSEN dasselbe
	// Paket ergeben, sonst kann die Deduplizierung nicht greifen.
	a := p.EncodeVibration(0.50)
	b := p.EncodeVibration(0.504)
	if string(a) != string(b) {
		t.Errorf("0.500 und 0.504 ergeben verschiedene Pakete (% x vs % x) - "+
			"dann lässt sich kein Schreibvorgang einsparen", a, b)
	}
}

// Vibration und Sog müssen unterscheidbare Pakete erzeugen. Wären sie
// gleich, könnte der Kanalzustand sie nicht auseinanderhalten und das
// Keepalive würde einen Kanal mit dem Paket des anderen überschreiben.
func TestVibrationAndSuctionPacketsDiffer(t *testing.T) {
	p := SamNeo2Protocol{}
	vibration := p.EncodeVibration(0.5)
	suction := p.EncodeSuction(0.5)
	if string(vibration) == string(suction) {
		t.Fatal("Vibration und Sog erzeugen dasselbe Paket")
	}
	if vibration[1] == suction[1] {
		t.Errorf("beide nutzen denselben Kommandotyp %#x - die Kanäle wären "+
			"nicht unterscheidbar", vibration[1])
	}
}
