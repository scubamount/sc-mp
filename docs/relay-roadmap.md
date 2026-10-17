# Relay roadmap

Imported from sc-offline PR #133 (commit 97b0b7a). The PR description, as written there:

## What this changes

Three changes to the external multiplayer plugin of #126, so it works with sco-relay, a small standalone relay server that players join (context: #134):

- **The server is never a player.** The plugin reads the server's reliable, host-only `multiplayer.relay` roster and keeps the server out of the peer list, the ghosts and the count. `relay_wire.h` is a byte-for-byte copy of the server's message contract.
- **Nearby-only forwarding.** When the roster says the server filters by distance, the plugin sends on `multiplayer.near_up` and listens on `multiplayer.near.<slot>`. Otherwise it behaves as before.
- **Smoother ghosts.** Ghosts move from a buffer of each player's last poses on a dejittered timeline, drawn 200 ms behind, with a short capped extrapolation. The plugin README ("How far the lead can be wrong") gives the measured bounds and what is not covered: senders slower than 10 Hz can still be led past the cap.

A session hosted by a player works as before. Consent, the 10 Hz cadence, the admission refusal and no-auto-join are unchanged.

It builds on #126 and starts from `main` at `4db58d2` (0.7.8). I can split it into two PRs (relay support, smoothing) if you prefer one change per PR.

## How it was tested

- [x] `tools/check.sh`: no new diagnostic against `main` at `4db58d2` (both report the same single diagnostic against the baseline, which is already on `main`)
- [x] Built the plugin Release x64 with MSVC 2022 (`/W4 /WX`, no warnings) and on Linux with `-Wall -Wextra -Werror`; the rest relies on CI's `build`
- [ ] Played in game on Windows
- [ ] `tools/update-test/run.sh` (not touched)

Tests added and run on Linux and Windows: the workflow test with the golden bytes of the new messages (266 checks); a motion test that runs the unmodified #126 module and this one on the same jittered, lossy 10 Hz path, plus buffer cases (548 checks; freezes 18 -> 1, largest step 2.71 m -> 1.02 m in one run); and, outside this PR, a join test against a real sco-relay over loopback UDP with synthetic game providers (239 checks).

The local builds used an SDK packaged from sco-core `82462b0`; `main` now pins SDK 2.2.0, which CI builds against.

Not tested: nothing was run in game. The production game shell refuses Join today (no admission collector), so an in-game join through a relay is not possible yet.

## Checklist

- [x] In scope: offline, no CIG servers or online play, connections only through LAN/VPN co-presence; no game files, secrets or personal data
- [x] PC changes the launcher makes are still listed and undone (the launcher is not touched)
- [x] Docs and `CHANGELOG.md` updated where players would notice