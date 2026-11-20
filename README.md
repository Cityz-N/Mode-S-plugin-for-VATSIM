# EHS Link

A free, simplified **Enhanced Mode S downlink** for VATSIM: pilots share their autopilot
selections (selected altitude, heading/track, IAS/Mach, V/S), and controllers see them in
EuroScope tags, with an alert when they don't match what was assigned.

Nothing travels over the VATSIM network. Data goes through a free public MQTT broker
(see [PROTOCOL.md](PROTOCOL.md)). No accounts, no servers to run, no paid software.

**Installation guide for pilots and controllers: https://cityz-n.github.io/Mode-S-plugin-for-VATSIM/**
· What's new: [CHANGELOG.md](CHANGELOG.md)

## Pilots (vPilot + MSFS)

Requirements: MSFS, vPilot. Nothing else (no FSUIPC).

> **Simulator support:** tested in **MSFS 2020**. **MSFS 2024 is untested**: the module and
> plugin are expected to work, but aircraft titles and the reworked default aircraft may need
> new profiles. Reports (and snapshots) from 2024 users are very welcome.

1. **Drag `Community\ehslink-module` into your MSFS Community folder.**
2. **Drag `vPilot\EhsLink.vPilot.dll` into vPilot's `Plugins` folder** (e.g. `C:\vPilot\Plugins`).
3. Restart MSFS and vPilot.
4. **Announce it in your flight plan (optional):** controllers then see you as EHS Link
   equipped even before your data arrives. Either:
   - add **`EHSLINK`** to your remarks (SimBrief *Remarks* field → `RMK/EHSLINK`), keeping
     your real Mode S code, or
   - set the SimBrief airframe's **Mode S code** to `FE5` + any 3 characters from `0-9 A-F`,
     e.g. **`FE5000`** (filed as `CODE/FE5000`). `F…` codes aren't allocated to real aircraft.
5. Connect to VATSIM. vPilot's message window shows which aircraft profile EHS Link is using.

PMDG aircraft need no extra setup: EHS Link reads the MCP values the aircraft publishes itself
(no `EnableDataBroadcast` options-file edit). If you made an options file read-only for an
earlier version, you can make it writable again.

**Aircraft support** comes from `profiles.ini` inside `ehslink-module`: one section per
aircraft, using the aircraft's own variables (A:vars, L:vars) or PMDG's data. Aircraft without
a profile use the simulator's standard autopilot variables, which suit default aircraft but
not study-level add-ons with their own FCU/MCP logic; those need a profile. Profiles marked
`verified = yes` have been checked against the real cockpit. New profiles are welcome as pull
requests (see the comments at the top of `profiles.ini`).

<!-- aircraft-list:start -->
| Aircraft | Status | MSFS 2020 | MSFS 2024 |
|---|---|:-:|:-:|
| A320neo (Asobo) | Verified | ✓ |  |
| A320neo V2 / A321neo / A330 / Beluga XL (iniBuilds, MSFS 2024 defaults) | Verified |  | ✓ |
| A32NX (FlyByWire) | Verified | ✓ | ✓ |
| A339 (Headwind) | Verified | ✓ | ✓ |
| A346 (Aerosoft) | Verified | ✓ |  |
| A350 (iniBuilds) | Verified | ✓ | ✓ |
| A380X (FlyByWire) | Verified | ✓ | ✓ |
| ATR 42/72-600 | Verified | ✓ | ✓ |
| Bluebird 757 | Verified | ✓ | ✓ |
| Boeing 737 MAX (Asobo) | Verified |  | ✓ |
| Boeing 747-8 | Verified |  | ✓ |
| Boeing 787 (HorizonSim / Asobo / Kuro) | Verified | ✓ | ✓ |
| Citation CJ4 | Verified |  | ✓ |
| Citation Longitude | Verified |  | ✓ |
| Citation X (FlightFX) | Verified | ✓ | ✓ |
| Fenix A319/A320/A321 | Verified |  | ✓ |
| FSLabs A32x | Verified |  | ✓ |
| FSS E-Jets | Verified |  | ✓ |
| iniBuilds A310 | Verified |  | ✓ |
| PMDG 737 | Verified | ✓ |  |
| PMDG 777 | Verified | ✓ | ✓ |
| TFDi MD-11 | Verified | ✓ |  |
| Vision Jet (Microsoft) | Verified |  | ✓ |
| Aerosoft CRJ | Draft |  |  |
| Boeing 787 | Draft |  |  |
| iFly 737 MAX | Draft |  |  |
| iniBuilds A300 | Draft |  |  |
| iniBuilds Airbus family | Draft |  |  |
| Just Flight BAe 146 | Draft |  |  |
| Just Flight F100 | Draft |  |  |
| Just Flight F28 | Draft |  |  |
| Learjet 35A | Draft |  |  |
| Leonardo MD-82 | Draft |  |  |
| LVFR A318/A319 | Draft |  |  |
| Synaptic A22X | Draft |  |  |

23 verified, 12 draft. A tick means checked in that simulator's cockpit.
<!-- aircraft-list:end -->

The [website](https://cityz-n.github.io/Mode-S-plugin-for-VATSIM/#aircraft) always shows the
current list (profiles update between releases).

Draft profiles were built from the community presets in **MobiFlight's HubHop** database
([hubhop.mobiflight.com](https://hubhop.mobiflight.com)) and from the WinWing / MobiFlight FCU
profiles that community members share on **flightsim.to** (only the variable names were used).
Thanks to the MobiFlight community and to those authors.
Controllers see draft-profile data marked `EHS~`. If you fly one of these, a snapshot and a
panel screenshot (below) is all it takes to verify it.

**Request support for your aircraft (no tools needed):**
1. Load the aircraft, power the autopilot panel, and dial **ALT 12300, HDG 123, SPD 234**
   (plus any V/S the aircraft allows).
2. Within a few seconds the module saves a snapshot of the aircraft's variables as
   `snapshot.txt` in its work folder:
   - MSFS 2020, MS Store: `%LOCALAPPDATA%\Packages\Microsoft.FlightSimulator_8wekyb3d8bbwe\LocalState\packages\ehslink-module\work\`
   - MSFS 2020, Steam: `%APPDATA%\Microsoft Flight Simulator\Packages\ehslink-module\work\`
   - MSFS 2024, MS Store: `%LOCALAPPDATA%\Packages\Microsoft.Limitless_8wekyb3d8bbwe\LocalState\WASM\MSFS2020\ehslink-module\work\`
     (yes, "MSFS2020": 2024 runs the module in its 2020 compatibility area)
   - MSFS 2024, Steam: probably `%APPDATA%\Microsoft Flight Simulator 2024\WASM\MSFS2020\ehslink-module\work\`
   - If it isn't there, search your PC for `snapshot.txt`.

   Nothing saved, or the aircraft won't take those values (e.g. 1000 ft altitude steps)? Keep
   them as close as you can and **set the transponder to 1230** for about 10 seconds: that
   always saves a snapshot, whatever the aircraft. Do it before connecting to VATSIM.
3. Open a GitHub issue with the aircraft name, a screenshot of the autopilot panel, and that
   file attached. Maintainers can also trigger a snapshot with `EhsTool snapshot 12300 123 234`.

**Profiles update themselves.** At every sim start the module downloads
`msfs-module/package/profiles.ini` from this repository and uses it when its `[Update] version`
is higher than the one it has. It keeps the copy in its work folder, so it also works offline.
Pilots never need to re-download the package for new aircraft. For maintainers:
- Bump `version` in `[Update]` whenever you change profiles.
- Downloaded files must parse, contain a fallback profile and be **read-only**: any expression
  that writes to the aircraft (`(>L:…`, `(>K:…`) makes the module reject the whole file.
- The download URL is only taken from the copy shipped inside the package, never from a
  downloaded file.

Optional `EhsLink.ini` next to the DLL:
```
brokers = broker.hivemq.com:1883, broker.emqx.io:1883
min_interval_ms = 1000
heartbeat_ms = 10000
```

## Controllers (EuroScope)

1. Copy `EhsLink.dll` anywhere (e.g. `%APPDATA%\EuroScope\PlugIn`) and load it in
   *Other SET > Plug-ins*.
2. Add the TAG items in the TAG editor:
   - `EHS selected altitude`: `240`
   - `EHS selected heading`: `270`, or `270T` in TRK mode
   - `EHS selected speed`: `250` or `M78` (`---` = window blank / managed)
   - `EHS selected V/S`: `-15` = -1500 ft/min (`---` = not in V/S mode)
   - `EHS summary (ALT HDG SPD)`: `240 270 M78`
   - `EHS current IAS` / `EHS current Mach`: the aircraft's **actual** speed (`251`, `M78`), updated about every 5 s. Information only, never coloured
   - `EHS altimeter setting`: `1013` (hPa) or `29.92` (inHg), in the pilot's own unit by default (`.ehs baro` to force one). STD shows as 1013 / 29.92
   - `EHS status`: `EHS~` (and dimmed colours on the other items) = data from an **unverified
     draft profile**: probably right, but not yet checked in that aircraft's cockpit. Hide such data with `.ehs unverified off`.
     `EHS` data arriving, `ehs` (grey) filed as equipped (`RMK/EHSLINK` or `CODE/FE5…`) but no data arriving (pilot's plugin not running), `EHS!` mismatch has lasted past the grace period, `EHS?` data position doesn't match the radar target (ignored)
3. **Colours (live):** each item compares the pilot's selection with your **cleared altitude,
   assigned heading, speed/Mach or rate**, and the altimeter setting with the expected one:

   | Colour | Meaning |
   |---|---|
   | **Green** (110,190,120) | exactly what you assigned |
   | **Blue** (100,160,235) | close: within 300 ft, 5°, 5 kt, M0.05, 200 ft/min, 1 hPa / 0.01 inHg |
   | **Red** (230,110,110) | further off, or wrong unit (Mach selected when you assigned IAS, and vice versa) |

   **Altimeter reference:** STD (1013 / 29.92) is expected above your transition altitude;
   QNH is expected below it, or when you've cleared the aircraft to an altitude below it.
   The QNH comes from the METAR of the nearer of the flight's origin and destination, so
   those airports need to be in your METAR list. No METAR means the altimeter stays in
   the normal tag colour. The comparison happens in the pilot's own unit.

   Items with nothing assigned keep the normal tag colour. The summary and status items show
   the worst colour of all fields. All three colours and all margins are yours to change
   (`.ehs color`, `.ehs tol`).
4. **Lists:**
   - The plugin adds an **"EHS Link" list** (`.ehs list` to open it) with every equipped
     aircraft in range, assigned value next to the pilot's selection:
     `C/S  TYPE  CFL SEL  AHDG SHDG  ASPD SSPD  IAS MACH  ARTE SV/S  QNH  EHS`.
     (Controllers who already opened the list keep their saved layout: add new columns,
     e.g. *EHS current IAS / Mach* or *EHS altimeter setting*, by hand.)
     The pilot columns use the same green/red colours. Clicking CFL, AHDG, ASPD (right-click
     for Mach) or ARTE opens EuroScope's usual edit popup, and clicking the callsign opens the
     flight plan. You can rearrange the columns, and EuroScope saves the layout.
   - The EHS items can also be added as columns to **any standard EuroScope list** (departure,
     arrival, SIL/SEL…): in the list's column setup, pick the *EHS Link* item provider.
5. **Notifications:** if a red mismatch lasts longer than the grace period (10 s), a message
   appears in the *EHS Link* chat tab (by default only for aircraft you're tracking).
   **Sound is off by default** (`.ehs sound on` to enable).

Commands:
```
.ehs                          status
.ehs list                     open the EHS Link list
.ehs code FE5                 flight plan marker prefix (CODE/FE5xxx)
.ehs baro pilot|hpa|inhg      altimeter display unit (default: pilot's unit)
.ehs unverified on|off        show data from unverified draft profiles (default on, marked EHS~)
.ehs color match 110,190,120  exact-match colour (R,G,B, or 'default')
.ehs color near 100,160,235   close-but-not-exact colour
.ehs color mismatch 230,110,110  mismatch colour
.ehs notify mine|all|off      who triggers chat notifications
.ehs sound on|off|C:\x.wav    notification sound (default off)
.ehs grace 10                 seconds a mismatch must last before alerting
.ehs tol alt 300              blue margins: alt (ft), hdg (deg), spd (kt), mach, vs (ft/min), qnh (hPa), inhg
.ehs brokers host[:port],...  broker list (must match the pilots')
```

## Releasing a new version

1. `.\set-version.ps1 0.3.0`: stamps the version into the vPilot plugin, EuroScope plugin and
   MSFS module (all parts always carry the same number).
2. Add a `## 0.3.0 — date` section at the top of [CHANGELOG.md](CHANGELOG.md): what changed,
   and who needs to update (pilots, controllers or both).
3. `.\package.ps1` → `release\EHS-Link-0.3.0.zip` (warns if the changelog entry is missing).
4. On GitHub, create a release tagged `v0.3.0`, paste the changelog section as its
   description and attach the zip.

Aircraft profile changes don't need a release: bump `[Update] version` in `profiles.ini`
and push; pilots get it at their next sim start. Run `.\update-aircraft-list.ps1` first so the
aircraft list in this README and the website fallback match `profiles.ini`.

## Building

- Everything + release zip: `package.ps1` (version from the `VERSION` file) → `release\EHS-Link-<version>.zip`.
- MSFS module: `msfs-module\build.ps1` (needs the free MSFS SDK; uses `MSFS_SDK`). Profile parser tests:
  `msfs-module\test\profiles_test.cpp` (`cl /std:c++17 /EHsc profiles_test.cpp`, run with `..\package\profiles.ini`).
- vPilot plugin: `dotnet build vpilot-plugin -c Release` (needs vPilot installed at `C:\vPilot`, or pass `-p:VpilotDir=...`).
- EuroScope plugin: `euroscope-plugin\build.bat` (needs VS 2022 Build Tools with C++; uses the EuroScope SDK in `%APPDATA%\EuroScope\PlugIn`, or set `ES_SDK`).
- Tests: `euroscope-plugin\build\ehs_test.exe` (unit tests), `ehs_test.exe listen 30 [CALLSIGN]` (watch the live broker, all pilots or one),
  `tools\ehs-tool`: `EhsTool sim CALLSIGN 30 [lat lon]` simulates a pilot; `EhsTool probe [s]` shows what the MSFS module reports; `EhsTool load <plugin.dll>` loads the vPilot plugin in a fake vPilot.

## Capacity

- Each pilot sends one ~300-byte message on change (max 1/s) plus a heartbeat every 10 s.
- Each controller only subscribes to the aircraft its EuroScope can see, typically 20–100, so a
  controller's traffic doesn't depend on how many pilots are online worldwide.
- Broker load grows with the number of pilots. A free public broker comfortably handles
  hundreds of pilots, but makes no promises. For a large rollout, run your own broker
  (e.g. Mosquitto, free) and point both plugins at it with the broker list setting.

## Limitations

- Only pilots running the plugin send data: treat it as an aid, never as a basis for separation.
- Public brokers have no uptime guarantee and anyone can publish; the controller plugin rejects
  data whose position is more than 10 NM from the radar target.

## License

MIT, see [LICENSE](LICENSE). EuroScope's plugin SDK and vPilot's plugin interface belong to
their authors and aren't included in this repository.
