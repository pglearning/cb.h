# cb.h examples

Every example is a standalone program: it includes `cb.h` (with `CB_IMPLEMENTATION`, the stb-style
switch) and is compiled on its own. Examples that build something write their outputs under
`build/examples/<name>/`, so the repository root stays clean.

```sh
cc -std=c99 -D_POSIX_C_SOURCE=200112L -I. examples/01_multi_project.c -o build/01_multi_project
./build/01_multi_project
```

| File | What it shows |
| --- | --- |
| `01_multi_project.c` | One table of projects (source dir, include dirs, link inputs, extra flags, optional run), sources collected recursively with `cb_walk_dir`, self-rebuilding build script |
| `02_single_project.c` | The minimal build: `cb_cc` / `cb_cc_flags` / `cb_cc_output` / `cb_cc_inputs` / `cb_cmd_run`, plus `cb_needs_rebuild` to skip work |
| `03_two_stage.c` | Generate a header from the build script, then compile a program that includes it |
| `04_build_api.c` | `CB_Cmd` options: render, `dont_reset`, redirection, raw fds, async runs with `max_procs`, `cb_chain_*` pipelines |
| `05_containers.c` | Dynamic arrays, sorting, bitset, ring buffer, `CB_Map` and `CB_Map_U64` |
| `06_strings.c` | `CB_String_Builder`, `CB_String_View`, UTF-8 helpers, case/parse/split/join tools |
| `07_filesystem.c` | `cb_walk_dir` (SKIP/STOP), paths, `cb_glob`, metadata, atomic write, symlink, mmap |
| `08_memory.c` | Arena, temp storage, `CB_ALLOC_TRACK` reporting |
| `09_logging.c` | Levels, `CB_LOG_AT`, the built-in handlers and a custom one |
| `10_toolbox.c` | Math/bits, time and date, RNG, environment, hex dump, CLI argument parsing |
| `11_config_switches.c` | The compile-time switches and the small macros (`CB_ARRAY_LEN`, `CB_ZERO`, `cb_swap`, `cb_shift`, `CB_UNUSED`) |

Notes that apply to all of them:

- `CB_IMPLEMENTATION` must be defined in exactly one translation unit before including `cb.h`; every
  other `.c` of the same program includes it without the switch.
- Building with strict `-std=c99` needs `-D_POSIX_C_SOURCE=200112L` on Linux; `cb_cc_flags` already
  puts it on the command line it generates for the targets it builds.
- `cb.h` writes no output on its own; `CB_ENABLE_ECHO` makes file system and command operations
  visible, which is what the `[INFO] CMD: ...` lines in these examples come from.
