# MeOS numeric BASIC

Uses the shared numeric functions. Start `basic`, then `HELP`,
or `basic FILE.bas`. Examples are in `user/basic/examples`. Includes floating
point math, bounded execution/cancellation, console INPUT, graphics and VFS
LOAD/SAVE/EDIT. EDIT explicitly saves the current listing through the editor.

`data` (`programs` is an alias) manages the opt-in persistent store at `/data`.
Keep BASIC sources in `/data/bas` and Scheme sources in `/data/scm`;
`mkdir /data/lua` adds a language directory. No initialization occurs
at boot. Use `tools/basic/prepare_programs_config.py` to create a separate config
and partition table; review layout and back up flash before installing it.
`data init` initializes its reserved partition; do not use without intent to
write it. `data mount` does not initialize blank/corrupt storage.

Run `sh tools/basic/host_test.sh`. Device evidence from the experimental combined
checkout does not qualify this integrated revision. Full device regression and flash
write/cache/power-loss acceptance remain required. MeOS licensing is unchanged;
uBASIC provenance and notices are in `third_party/ubasic`.

SAVE and EDIT use whole-buffer VFS replacement. Transactional M5FS preserves a
complete old/new file across uncertain commits; raw RAM/FAT and other legacy
mounts keep their existing streaming semantics. Errors retain the source buffer
and never retry through a different save method. See `docs/atomic-save.md`.
