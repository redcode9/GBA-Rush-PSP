## CHANGELOG entry

<!-- The entry this change adds to CHANGELOG.md, written as behaviour a player
can see, not as code or test changes. Write "none" for a change nobody sees. -->

## Regression proof

<!-- For a bug fix, paste the output of
`make prove-regression REF=<last release tag> TEST=<test name>`: it should say
FAIL-AS-EXPECTED. Write "none" and why otherwise. -->

```text

```

## Hardware results

<!-- The rows this change adds to docs/HARDWARE-RESULTS.md. Changes to PSP
rendering, MIPS assembly, radio, power callbacks, threads, audio, networking
or file access need rows from a console. Write "none" and why otherwise. -->

## Host checks

<!-- Paste the last lines of each command, or say which one could not run. -->

- [ ] `make -j4 check HOST_CC='cc -O1'` passes
- [ ] `make -j4 test-sanitize HOST_CC='cc -O1'` passes
- [ ] `make -j4 psp-build` builds without warnings

```text

```
