#!/usr/bin/env bash
#
# 内存/未定义行为检查脚本（ASan + UBSan + LeakSanitizer）
#
# 用途：把 cb.h 的测试在检测器下重跑一遍，抓出普通测试发现不了的问题。
#
# 用法：
#     tools/verify-sanitizers.sh                # 全部测试
#     tools/verify-sanitizers.sh arena map      # 只跑指定的测试
#     tools/verify-sanitizers.sh --examples     # 跑 examples/ 下的全部示例
#
# 为什么需要这个
# --------------
# 这个脚本不是摆设，它一上来就抓到了两类真问题：
#   1. cb_da_append_many 在 n == 0 时对 NULL 调 memmove，并做了 NULL 指针算术。
#      C 标准里两者都是 UB（cb_cmd_extend(&空, &空) 正好走这条路）。
#      gcc/clang 的普通构建看不出来，-fsanitize=undefined 直接报。
#   2. 四个测试自己分配的缓冲区没释放（arena 忘了 cb_arena_free 导致 1MB 泄漏）。
# 所以"测试全绿"不等于"没问题"，检测器是另一条独立的证据链。
#
# 关于 LeakSanitizer：cb.h 的 temp storage 是 _Thread_local 的、生命周期到进程结束，
# 故意不释放，这是设计而不是泄漏；测试里不要为它写 free。除此之外一律要求 0 泄漏。

set -euo pipefail

cd "$(dirname "$0")/.."

CC=${CC:-clang}
SAN=${SAN:-address,undefined}
BUILD_DIR=${BUILD_DIR:-$(mktemp -d)}

# 与 cb.c 里的 test_names[] 保持一致
ALL_TESTS=(arena bytes_for_utf8 chain dynamic_array error_paths fs map nob_parity
           read_entire_dir stdlib string_builder string_view strip_prefix temp_storage toolbox)

if [ "$#" -gt 0 ]; then
    TESTS=("$@")
else
    TESTS=("${ALL_TESTS[@]}")
fi

echo "== 编译器 =="
$CC --version | head -1
echo "== 检测器: $SAN =="
echo "== 构建目录: $BUILD_DIR =="

# abort_on_error=0：让检测器报完继续跑，一次看到全部问题，而不是第一个就停
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:strict_string_checks=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0"

# --examples：示例的覆盖面比测试宽（每个示例专门演示一个模块），值得单独扫一遍。
# 与 ./cb examples 一致：示例在**仓库根目录**下运行，它们自己负责清理产生的文件。
if [ "${1:-}" = "--examples" ]; then
    shift
    failed=0
    for src in examples/*.c; do
        name=$(basename "$src" .c)
        extra=()
        [ "$name" = "11_memory" ] && extra+=(-DCB_ALLOC_TRACK)
        [ "$name" = "12_logging" ] && extra+=(-rdynamic)

        if ! $CC -std=gnu11 -g -O1 -fsanitize="$SAN" -fno-omit-frame-pointer -I. \
                 "${extra[@]}" -o "$BUILD_DIR/$name" "$src"; then
            echo "   FAIL $name（编译失败）"
            failed=$((failed + 1))
            continue
        fi

        out=$(timeout 300 "$BUILD_DIR/$name" 2>&1) || true
        if echo "$out" | grep -qE "runtime error|AddressSanitizer|LeakSanitizer"; then
            echo "   FAIL $name"
            echo "$out" | grep -E "runtime error|AddressSanitizer|LeakSanitizer|cb\.h:[0-9]+" | head -12
            failed=$((failed + 1))
        else
            echo "   ok   $name"
        fi
    done

    echo
    if [ "$failed" -eq 0 ]; then
        echo "== 全部示例在 $SAN 下干净 =="
    else
        echo "== 有 $failed 个示例在检测器下报错 =="
    fi
    echo
    echo "临时文件在 $BUILD_DIR（可自行删除）"
    exit "$failed"
fi

failed=0
for name in "${TESTS[@]}"; do
    src="tests/$name.c"
    [ -f "$src" ] || { echo "   ?? 找不到 $src"; failed=$((failed + 1)); continue; }

    # -g 为了能定位到 cb.h 的行号；-O1 保留一点真实代码形态又便于定位
    if ! $CC -std=gnu11 -g -O1 -fsanitize="$SAN" -fno-omit-frame-pointer -I. \
             -o "$BUILD_DIR/$name" "$src"; then
        echo "   FAIL $name（编译失败）"
        failed=$((failed + 1))
        continue
    fi

    run_dir="$BUILD_DIR/run_$name"
    mkdir -p "$run_dir"
    out=$(cd "$run_dir" && timeout 300 "$BUILD_DIR/$name" 2>&1) || true

    if echo "$out" | grep -qE "runtime error|AddressSanitizer|LeakSanitizer|FAILED"; then
        echo "   FAIL $name"
        echo "$out" | grep -E "runtime error|AddressSanitizer|LeakSanitizer|FAILED|cb\.h:[0-9]+" | head -12
        failed=$((failed + 1))
    else
        echo "   ok   $name"
    fi
done

echo
if [ "$failed" -eq 0 ]; then
    echo "== ${#TESTS[@]} 个测试在 $SAN 下全部干净 =="
else
    echo "== 有 $failed 个测试在检测器下报错 =="
fi

echo
echo "临时文件在 $BUILD_DIR（可自行删除）"
exit "$failed"
