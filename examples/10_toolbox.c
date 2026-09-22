// Toolbox: math and bits, time and date, RNG, environment, terminal and CLI argument parsing.
#define CB_IMPLEMENTATION
#include "cb.h"

int main(int argc, char** argv)
{
    // ---- math and bits -----------------------------------------------------------------------
    printf("min/max/clamp: %d %d %d\n", cb_min(3, 5), cb_max(3, 5), cb_clamp(11, 0, 9));
    printf("align: up(13,8) = %llu, down(13,8) = %llu\n",
           (unsigned long long)cb_align_up(13, 8), (unsigned long long)cb_align_down(13, 8));
    printf("pow2: is_pow2(64) = %d, next_pow2(100) = %llu\n",
           cb_is_pow2(64), (unsigned long long)cb_next_pow2(100));
    printf("bits: popcount(0xFF00) = %d, ctz(8) = %d, clz(1) = %d\n",
           cb_popcount64(0xFF00), cb_ctz64(8), cb_clz64(1));
    printf("bytes: bswap32(0x11223344) = 0x%08X, little endian = %d\n",
           cb_bswap32(0x11223344u), cb_is_little_endian());

    // ---- time and date -----------------------------------------------------------------------
    int64_t now = cb_time_now();
    printf("time: now = %lld, iso8601 = %s\n", (long long)now, cb_time_to_iso8601(now));
    printf("duration: %s / %s / %s\n",
           cb_duration_to_str(0.42), cb_duration_to_str(12.5), cb_duration_to_str(3725.0));

    // ---- RNG (xoshiro256**, deterministic for a given seed) ---------------------------------
    CB_Rng rng;
    cb_rng_seed(&rng, 42);
    printf("rng: %llu %llu, double in [0,1) = %.3f, range(10) = %llu\n",
           (unsigned long long)cb_rng_next(&rng), (unsigned long long)cb_rng_next(&rng),
           cb_rng_double(&rng), (unsigned long long)cb_rng_range(&rng, 10));

    // ---- environment and terminal -------------------------------------------------------------
    const char* home = cb_env_get("HOME");
    printf("env: HOME = %s, stdout is a tty = %d, width = %d\n",
           home != NULL ? "(set)" : "(unset)", cb_stdout_is_tty(), cb_terminal_width());

    // ---- hex dump (always stderr) --------------------------------------------------------------
    const unsigned char bytes[] = {0x00, 0x01, 'A', 'B', 0x7F, 0x80, 0xFF};
    cb_dump_hex(bytes, sizeof(bytes));

    // ---- CLI arguments: --key=value, --flag, -f, and positionals ------------------------------
    CB_Args args = CB_ZERO;
    cb_args_parse(&args, argc, argv);
    printf("args: has(--verbose) = %d, get(--out, default) = %s, positionals = %zu\n",
           cb_args_has(&args, "verbose"), cb_args_get(&args, "out", "(none)"),
           cb_args_positional_count(&args));
    for (size_t i = 0; i < cb_args_positional_count(&args); ++i) {
        printf("  positional[%zu] = %s\n", i, cb_args_positional(&args, i));
    }
    cb_args_free(&args);
    return 0;
}
