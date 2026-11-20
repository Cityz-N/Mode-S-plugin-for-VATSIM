# Changelog

All notable changes to EHS Link. Versions follow `major.minor.patch`.

Aircraft profiles are versioned separately (`[Update] version` in `profiles.ini`) and reach
pilots automatically at every sim start, so new or fixed aircraft don't need a new release.

## 0.2.2 — 2026-11-20

**Who needs to update:** controllers who want to share EHS data with other plugins
(`EhsLink.dll`). Pilots don't need to update: their files only changed version number.

### Controllers
- **EuroScope Plugin Bridge support** (contributed by Alexis Balzano): when the bridge
  ([EuroScopeBridge.dll](https://github.com/AlexisBalzano/Euroscope-Plugin-Bridge/releases),
  loaded separately under *Other Settings → Plug-ins*) is present, EHS Link publishes each
  aircraft's data for other plugins under the provider id `ehslink`: `sel_alt`, `sel_hdg`,
  `sel_trk`, `sel_ias`, `sel_mach`, `sel_vs`, `baro`, `ias`, `mach`, `verified`
  (`.esb schema ehslink`). Only fresh, position-checked data is published. The bridge is
  optional: without it EHS Link works exactly as before. This lets other plugins, such as
  CoFrance, show the pilots' selections.

### Aircraft (profiles, automatic — no download needed)
- Verified in MSFS 2024 from the first community snapshots: Fenix A319/A320/A321 and FSLabs A32x.

## 0.2.1 — 2026-11-18

**Who needs to update:** controllers (`EhsLink.dll`), and pilots who want to send aircraft
snapshots (`Community\ehslink-module`). The vPilot plugin is unchanged.

### Pilots
- **Snapshots are more reliable:** the automatic snapshot (ALT 12300 + HDG 123) now also
  triggers on aircraft whose heading knob turns smoothly (the A310 holds 123.55 for "123").
- **Squawk 1230 saves a snapshot** in any aircraft, even when 12300 can't be dialled or the
  autopilot values aren't readable. Squawk something else and 1230 again for a fresh one.

### Controllers
- **Stability:** a controller's EuroScope crashed some time after loading EHS Link (busy setup
  with many plugins). The plugin is now hardened against that kind of crash:
  - the EHS Link list no longer re-adds every row each second because current IAS/Mach
    changed; those columns refresh every 10 s, selections and colours still update at once;
  - out-of-range values in pilot data are ignored instead of being formatted;
  - text formatting truncates instead of aborting, incoming messages are checked before use,
    and no error in the plugin or its network thread can propagate into EuroScope.

### Aircraft (profiles, automatic — no download needed)
- Verified in MSFS 2024: iniBuilds A320neo V2 / A321neo / A330 and A310, Asobo 737 MAX and
  747-8, ATR, FBW A32NX, PMDG 777, Bluebird 757, 787, iniBuilds A350, FSS E-Jets.
- A310 heading now matches the window (was 1° high); default 737 MAX shows knots when flying
  Mach (the aircraft doesn't expose Mach mode).

## 0.2.0 — 2026-11-16

**Who needs to update:** everyone (pilots: both files; controllers: `EhsLink.dll`).

### Pilots
- **No more FSUIPC.** A small module in the MSFS Community folder reads the aircraft directly;
  installation is now two drag-and-drops.
- **PMDG needs no setup:** the 777 and 737 are read from the aircraft's own variables, so the
  `EnableDataBroadcast` / read-only options file step is gone (custom 777 builds work too).
- **Aircraft profiles** for 16 verified aircraft and ~19 drafts, **updated automatically** from
  GitHub at every sim start.
- **Request your aircraft:** dial ALT 12300 / HDG 123 / SPD 234 and the module saves
  `snapshot.txt` to attach to a GitHub issue.
- **New data sent:** altimeter setting (hPa/inHg) and current IAS/Mach (about every 5 s).
- **Flight plan marker:** `RMK/EHSLINK` in the remarks as an alternative to `CODE/FE5000`, so you
  can keep your real Mode S code.
- Managed (dashed) FCU/MCP windows are sent as `---` instead of the managed target.
- Fixed: vPilot could crash when closing.
- MSFS 2024: the module no longer declares a 2020-only minimum version (2024 still untested).

### Controllers
- **Three colours:** green = exactly as assigned, blue = close, red = off (or wrong unit).
  All colours and "close" margins are configurable (`.ehs color`, `.ehs tol`).
- **Altimeter check:** STD above the transition altitude, QNH from the METAR of the nearer
  of departure/destination below it or when cleared to an altitude. hPa shown like an altimeter
  (decimals cut, not rounded).
- **EHS Link list** (`.ehs list`): your clearance next to the pilot's selection, with
  EuroScope's usual edit popups; rows refresh live.
- New tag items: altimeter setting, current IAS, current Mach.
- Draft-profile data is marked `EHS~` with dimmed colours (`.ehs unverified off` to hide it).
- Pilots filed with `RMK/EHSLINK` or `CODE/FE5…` but not sending data show a grey `ehs`.
- Sound alerts are off by default (`.ehs sound on`).
- Only subscribes to aircraft in range, so it scales with the number of pilots.

## 0.1.0 — 2026-11-15

First test release: vPilot plugin reading the autopilot through FSUIPC, public MQTT broker,
EuroScope plugin with tag items and mismatch alerts.
