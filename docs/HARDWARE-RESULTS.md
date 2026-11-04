# Hardware results

Tests run on real PSPs. Every result belongs to the build that ran, named
by the SHA-256 of its `EBOOT.PBP`: the first 12 hex digits in the results
table, the full hash in the builds table. Logs and Diagnostics reports
behind each row stay out of Git, in the `artifacts/hardware/` folder of the
main checkout that `scripts/install-test-build.sh` wrote when it collected
them.

## Builds

| EBOOT.PBP | Full SHA-256 | Built from | Notes |
|---|---|---|---|
| `5dfde160d28b` | `5dfde160d28be296b91890fd8be009c0fb00fb18605bb618bd08b0e91302438f` | `features/state-of-the-art-development/main` 700de75, pspdev v20260801 | Test build with the New core, the Exact renderer on the Media Engine and the built-in BIOS. `exception.prx` 75d7347e9f58…, `media_engine.prx` 10cd22cb758c…. Reports itself as 1.4.3. |

## Results

| Date | Version | EBOOT.PBP | Model | Firmware/CFW | Storage | Test | Result | Minutes | Evidence |
|---|---|---|---|---|---|---|---|---|---|
