# Device support — today and direction

**Owner, 11 Oct 2026:**
- The Sam Neo 2 is the first and main device, and it stays fully supported.
- Most users, though, have other devices: strokers like **The Handy**, multi-axis strokers, vibrators.
- SamNPlayer must support these **fully**.
- The curves and controls of a device must **appear when that device is connected**.

This is planned **after** the current V1 workspace work (W1–W4). It is recorded here so every step until then keeps it in mind.

## Today

| Device / path | Channels used | Notes |
|---|---|---|
| **Sam Neo 2**, own BLE | Vibration, Suction | First-class: diagnostics, battery, recipes (stroke → feel) |
| **Intiface Central (Buttplug)** | Stroke via `LinearCmd`; every vibration motor, suction, oscillate and rotate via `ScalarCmd` / `RotateCmd` | D2: strokers (The Handy) get the script's real positions; multi-motor devices (LELO F1S V3) drive every motor |
| **TCode over Wi-Fi** (D5) | Stroke (L0) | OSR2 / SR6 and other TCode devices with network firmware; write-only, UDP or TCP |
| **Mock** | Vibration, Suction | Tests and demo |

The device interface (`device.Device`) has two intensity channels: `SetVibration` and `SetSuction`. The **Stroke** curve drives the Neo 2 feel recipe. Devices with a linear channel also implement `device.LinearDevice` (`SetPosition`); the player sends them the script's stroke positions (D2).

### How D2 drives a Buttplug device

| Feature | Follows |
|---|---|
| Every `Vibrate` motor | Vibration curve (one `ScalarCmd` for all motors) |
| First `Constrict` / `Inflate` (or `Oscillate` when there is no Constrict) | Suction curve |
| Other `Oscillate`, `Rotate` scalars, every `RotateCmd` (clockwise) | Vibration curve as intensity / speed |
| First `LinearCmd` | Stroke positions: at each playback position the **next** action is sent as "move to pos within the time until it is due", once per action. Playback speed shortens the move time; pause stops the device; a seek resends |
| Scalar `Position` | Listed, not driven |

## Direction: device-aware channels

1. **Capabilities.** Every connection reports which channels the device has:

   | Channel | Example devices |
   |---|---|
   | `stroke` (L0, linear position) | The Handy, Kiiroo Keon, OSR2 / SR6 |
   | `surge` / `sway` (L1 / L2) | SR6 |
   | `twist` / `roll` / `pitch` (R0–R2) | OSR2 / SR6 |
   | `vibrate` (one or more motors) | Lovense, Neo 2 |
   | `rotate` | Rotating devices |
   | `suction` | Neo 2 |

2. **The Player shows what is connected.** The following follow the connected device's capabilities:
   - the Inspector → Devices rows (toggle, level, live value);
   - the timeline lanes, with names and colours.

   Without a device, the view shows what the loaded script contains. Nothing is shown that the device cannot do. This replaces the V1 rule "no Axis 3/4". That rule was right for the Neo 2 only.

3. **Scripts.**
   - Multi-axis funscripts are loaded next to the main script: `name.surge.funscript`, `.twist.funscript`, `.roll.funscript`, `.pitch.funscript`, and so on.
   - `.samn` keeps its Neo 2 axes.
   - Export writes the axes a device needs.

4. **Transports.**
   - **Intiface / Buttplug:** `LinearCmd` (stroke with duration), `RotateCmd`, `ScalarCmd` per feature. This covers The Handy over Bluetooth and many others.
   - **TCode over serial / Wi-Fi:** for OSR2 / SR6.
   - **The Handy online API (HSSP):** optional, later.

5. **Generator.** Create keeps producing the stroke curve every stroker needs. Neo 2 feel channels stay as they are. Extra axes come only where the video motion supports them.

## Named devices (Owner)

The Owner named these devices as must-support:

| Device | Channels (expected over Buttplug / Intiface) | Path |
|---|---|---|
| **Sam Neo 2** | vibration, suction | Own BLE plus Intiface (first-class) |
| **The Handy** | stroke (linear) | Intiface `LinearCmd` ✅ (D2); online API optional (D5) |
| **LELO F1S V3** | 2 × vibration (motors) | Intiface `ScalarCmd` Vibrate, both motors ✅ (D2) |
| **Lovense Max 2** | vibration and constrict (air pump → suction) | Intiface `ScalarCmd` Vibrate + Constrict ✅ |

The expected channels follow from the public Buttplug device descriptions. The real report comes from the device on connect (D1), so the GUI never relies on this table.

## Steps (after V1 W4)

| # | Step |
|---|---|
| D1 ✅ | Capability model in Go (`device.Capabilities`); Neo 2, Mock and Intiface report theirs; the GUI receives them on connect |
| D2 ✅ | Intiface `LinearCmd` and `RotateCmd`, plus vibrate per motor; the Player sends the stroke position to linear devices |
| D3 (part 1 ✅) | Player UI follows capabilities. **Done:** Inspector → Devices hides rows for channels the connected device lacks; a stroker gets stroke on/off and a **safety range** (bottom / top, at least 10 % apart, positions scaled into it, live via `SetStrokeOutput`). **Per-motor levels ✅:** a device with two or more vibration motors (LELO F1S V3) gets one level slider per motor (`SetMotorLevels`, `device.MotorLevelSetter`; saved, re-applied on connect). **Open:** timeline lanes per device |
| D4 (part 1 ✅) | Multi-axis script loading: companion files `<name>.surge/.sway/.twist/.roll/.pitch.funscript` beside the script are read (inversion honoured) and shown as read-only lanes in their own colour (`funscript.LoadCompanionAxis`, `GetScriptAxisActions`). **Output ✅:** a TCode device (SR6 / OSR2) gets them as L1 / L2 / R0–R2 moves (`device.MultiAxisDevice`, `Player.SetAxes`; next action per axis, once; stroke off switches them off; the stroke safety range does not scale them). **Open:** editing and export |
| D5 (part 1 ✅) | **TCode over Wi-Fi** (OSR2 / SR6 Wi-Fi firmware, UDP or TCP, default port 8000): Tools → Device → "TCode over Wi-Fi"; the stroke (L0) follows the script with the D3 safety range, `DSTOP` on pause / emergency stop (`device.TCode`). **Open:** TCode over USB serial (needs a serial-port dependency — Owner decision), optional Handy online API. Extra axes ✅ (see D4) |

Each step comes with tests, using a mock device per capability set.
