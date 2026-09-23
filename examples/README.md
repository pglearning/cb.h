# cb.h examples

`examples/` holds build-script examples only: two scripts plus the two demo projects they build.
Everything that demonstrates a runtime API (containers, strings, file system, logging, ...) lives
in `tests/`, where every test prints its results and is compared against a recorded golden output.

Run them from the repository root; the paths in `projects[]` are relative to the working directory:

```sh
cc -std=c99 -D_POSIX_C_SOURCE=200112L -I. examples/01_single_project.c -o build/01_single_project
./build/01_single_project   # builds ./bin/core.so
./build/02_multi_project    # builds ./bin/core.so and ./bin/app, then runs the app
```

| File | What it shows |
| --- | --- |
| `01_single_project.c` | One `Project` entry: a shared library built from a project directory |
| `02_multi_project.c` | Two entries: the library first, then the app that links it (include dir, link input, run step) |

## The shape of a build script

```c
typedef struct {
    const char* const * extra_flags; // NULL-terminated switches, (const char* const[]){...}
    const char* include_dir;         // becomes -I<dir>, may be NULL
    const char* source_dir;          // walked recursively for the *.c inputs
    const char* output;              // file name inside OUTPUT_FOLDER
    bool should_run;                 // run the output after a successful build?
    const char* run_arg;             // first argument of that run, may be NULL
} Project;
```

- adding a project is one entry in `projects[]`; nothing else in the script changes;
- `collect_sources` feeds the compiler inputs — only `.c`, a header must never be an input;
  `collect_include` sits next to it for collecting a directory's `.h` files (the scripts do not call
  it yet; hook it to `walk_dir` when header changes should also trigger a rebuild);
- the command is `cc()` + `cc_flags()` + `cc_output()` (platform and language aware flags and `-o`),
  then the sources, then `extra_flags` **last**: with `--as-needed` (Ubuntu's default) a library
  listed before the objects that need it is dropped and the link fails;
- `project_run` runs the built program and keeps its stdout next to it:
  `./bin/<output>.stdout.txt`.

| Demo project | Layout | Output |
| --- | --- | --- |
| `dynamic_library/` | `src/core.c`, `src/mathx.c`, `include/core.h` | `bin/core.so` |
| `app/` | `src/main.c`, `src/report.c`, `include/report.h` | `bin/app`, plus `bin/app.stdout.txt` |

## Notes

- `CB_IMPLEMENTATION` in exactly one translation unit; `CB_ENABLE_ECHO` prints the generated
  commands; `CB_STRIP_PREFIX` lets the script write `walk_dir()` / `cmd_run()` / `temp_sprintf()`
  without the `cb_` prefix. Two names keep it: `ERROR` (mingw `<wingdi.h>`) and `log` (libm),
  hence `cb_log(INFO, ...)`.
- `examples/cb.h` is a copy of the root `cb.h`, not a symlink: a symlink breaks in a `cp -r` copy and
  cannot be checked out on Windows. Keep them in sync with `cp cb.h examples/cb.h`;
  `tools/verify-examples.sh` and CI fail when they differ.
- Outputs land in `bin/` at the repository root, which is why `bin/` is ignored.
- Windows / macOS: edit `output` and `extra_flags` in the table (`core.dll` with `-shared`,
  `-Wl,-rpath,@loader_path` on macOS).
