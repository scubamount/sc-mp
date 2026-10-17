# Provenance

Extracted from scubamount/sc-offline v0.7.8 (commit `4db58d237c5f77ef6b017c135a91970ae7806838`),
paths `plugins/multiplayer/` and `tools/multiplayer_workflow_test.cpp`, unchanged. GPL-3.0-only, as
in sc-offline. Consumes the sco-core SDK 2.2.0 (`sdk-v2.2.0`,
`686187ce936400450d448f1fe754b1e58263e4af`) public headers only.

The `relay-import` branch adds sc-offline PR #133 (commit `97b0b7a`), restricted to the module's own
files; that PR's CHANGELOG and `docs/features.md` hunks belong to sc-offline and were left out.

sc-offline's own lineage and third-party notices:
[THIRD_PARTY_NOTICES.md](https://github.com/scubamount/sc-offline/blob/main/THIRD_PARTY_NOTICES.md).

## Outside contributions

Code from anyone other than the maintainer is recorded here when it lands, under the terms in
[CONTRIBUTING.md](CONTRIBUTING.md). One row per contribution:

| Contributor (GitHub) | What and where | Source | Terms it came in under | Credit in history | Owner review |
| --- | --- | --- | --- | --- | --- |
| `zarilo88-ui` | The relay-support and ghost-smoothing work on this branch: `plugins/multiplayer/relay_wire.h`, `plugins/multiplayer/motion.h`, the relay and nearby-only paths in `plugins/multiplayer/multiplayer.cpp`, `plugins/multiplayer/.gitattributes`, `tools/multiplayer_motion_test.cpp`, additions to `tools/multiplayer_workflow_test.cpp` and `plugins/multiplayer/README.md`, `docs/relay-roadmap.md`, and the motion test target in `plugins/multiplayer/CMakeLists.txt` | sc-offline pull request #133 (closed), commit `97b0b7a`; `relay_wire.h` states it is a byte-for-byte copy of the contract of a relay server (sco-relay) whose source is not in this repository | Pull request to sc-offline; sc-offline's CONTRIBUTING says "By opening a pull request you agree your contribution is licensed under GPL-3.0-only." (see [CONTRIBUTING.md](CONTRIBUTING.md)) | Commit `1ad9e7b` is authored `scubamount` with `Co-authored-by: zarilo88-ui` and a `Co-Authored-By` trailer for an AI assistant; the merge of this branch keeps that commit | pending |
| `zarilo88-ui` | The idea of the bounded roster read past 16 members in `plugins/multiplayer/multiplayer.cpp` (`Members`), re-implemented by the maintainer in commit `64c2dbe`, which names it; no code of sc-mp pull request #1 was merged | sc-mp pull request #1 (open) | Pull request to sc-mp; no licence text of sc-mp's own applies yet (see the gaps in [CONTRIBUTING.md](CONTRIBUTING.md)) | Named in the commit message of `64c2dbe` | pending |

Not recorded as settled: the rights in the relay-support work, and whether the terms quoted above are the ones
that govern sc-mp. Both are for the maintainer. "Pending" in the last column means the maintainer has not yet
reviewed the row.
