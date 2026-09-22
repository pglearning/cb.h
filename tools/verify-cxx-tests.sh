#!/usr/bin/env bash
#
# C++ 模式验证脚本（clang++ / g++ × c++17 / c++20）
#
# 用途
# ----
# CI 的 linux job 用 C 与 C++ 两种语言模式构建 cb（`$cc -x c++ -o cb cb.c`）。
# C++ 模式下 cb.c 里那份 cb_cc_flags 会带 -x c++，于是 `./cb test` 把全部测试当 C++
# 编译——而本地开发者通常只跑 C 模式，于是出现"本地 15/15、CI 上 C++ job 全红"。
# 这个脚本把 C++ 那条路径搬到本地，提交前就能看见。
#
# 它抓到过的两类问题（都只在 C++ 下暴露，C 下永远不报）：
#   * 指定初始化器的 designator 顺序：C 允许乱序，C++ 要求与结构体声明顺序一致；
#   * {0} 部分初始化：C 不告警，C++ 的 -Wmissing-field-initializers 会逐个成员报。
#
# 用法
# ----
#     tools/verify-cxx-tests.sh                  # 全部检查
#     tools/verify-cxx-tests.sh --quick          # 只跑 g++ -std=c++17
#     CXX_SPECS="clang++ -std=c++23" tools/verify-cxx-tests.sh   # 自定义组合
#
# 检查内容
# ----
#   1. clang++ / g++ × c++17 / c++20 构建 cb.c：要求 0 错误 0 告警；
#   2. 每个构建出的 cb 跑 `./cb test`：要求 N/N 全过（N 取自 `./cb list`，不写死数字）
#      且编译这 15 个测试时 0 告警；
#   3. 再用 clang++ / g++ 直接把这批 tests/*.c 编成 C++：要求 0 告警。
#      `./cb test` 用的 cc 在多数机器上是 gcc，clang 专有的告警只有这样才抓得到；
#   4. 同样扫一遍 examples/。

set -uo pipefail

cd "$(dirname "$0")/.."

QUICK=0
if [ "${1:-}" = "--quick" ]; then
    QUICK=1
fi

if [ -n "${CXX_SPECS:-}" ]; then
    read -r -a SPECS <<<"$CXX_SPECS"
else
    SPECS=("clang++ -std=c++17" "g++ -std=c++17" "clang++ -std=c++20" "g++ -std=c++20")
    [ "$QUICK" = "1" ] && SPECS=("g++ -std=c++17")
fi

# 扫描用的编译器组合：与构建用的 C++ 编译器一致（去掉标准版本去重）
SWEEP_SPECS=("clang++ -std=c++17" "g++ -std=c++17" "clang++ -std=c++20" "g++ -std=c++20")
[ "$QUICK" = "1" ] && SWEEP_SPECS=("g++ -std=c++17")

WARN_RE='warning:|error:'
BUILD_DIR=${BUILD_DIR:-$(mktemp -d)}
failed=0

# 统一入口：跑一条命令，把 stderr+stdout 存进 $out，命令失败算失败
run_capture() {
    local desc=$1
    shift
    out=$("$@" 2>&1)
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "   FAIL $desc（退出码 $rc）"
        printf '%s\n' "$out" | tail -25
        failed=$((failed + 1))
        return 1
    fi
    if printf '%s' "$out" | grep -qE "$WARN_RE"; then
        echo "   FAIL $desc（有告警或错误）"
        printf '%s\n' "$out" | grep -E "$WARN_RE" | head -20
        failed=$((failed + 1))
        return 1
    fi
    return 0
}

echo "== 编译器 =="
clang++ --version | head -1
g++ --version | head -1
echo "== 构建目录: $BUILD_DIR =="

check_binary() {
    local spec=$1
    local tag
    tag=$(printf '%s' "$spec" | tr -c 'A-Za-z0-9' '_')
    local bin="$BUILD_DIR/cb_$tag"

    echo
    echo "== $spec =="

    echo "-- 1) 构建 cb.c（要求 0 告警）"
    # shellcheck disable=SC2086
    run_capture "构建 $spec" $spec -Wall -Wextra -I. -x c++ -o "$bin" cb.c || return
    echo "   ok"

    echo "-- 2) 跑 ./cb test（要求全部通过且 0 告警）"
    # 测试条数不写死：从 ./cb list 数出来，加了测试也不用改这里
    local listed
    listed=$("$bin" list 2>&1 | grep -c '^\[INFO\]     ')
    if [ "$listed" -eq 0 ]; then
        echo "   FAIL 数不出测试条数（./cb list 的输出格式变了？）"
        failed=$((failed + 1))
        return
    fi
    # 注意 ./cb test 会先跑 check_cxx_compat（内部又用 clang++/g++ 编一次 cb.c），
    # 所以这条命令同时也在检查构建脚本自身的 C++ 兼容性。
    run_capture "./cb test（$spec 构建出的 cb）" "$bin" test || return
    local passed
    passed=$(printf '%s' "$out" | grep -oE '[0-9]+/[0-9]+ test\(s\) passed' | tail -1)
    if [ "$passed" != "$listed/$listed test(s) passed" ]; then
        echo "   FAIL 期望 $listed/$listed test(s) passed，实际为 '${passed:-（没有这一行）}'"
        failed=$((failed + 1))
        return
    fi
    echo "   ok  $passed"
}

for spec in "${SPECS[@]}"; do
    check_binary "$spec"
done

echo
echo "== 直接把源文件按 C++ 编译一遍（0 告警）=="
for spec in "${SWEEP_SPECS[@]}"; do
    count=0
    bad=0
    for src in tests/*.c examples/*.c; do
        [ -f "$src" ] || continue
        count=$((count + 1))
        # shellcheck disable=SC2086
        out=$($spec -Wall -Wextra -Wswitch-enum -I. -x c++ -fsyntax-only "$src" 2>&1)
        if printf '%s' "$out" | grep -qE "$WARN_RE"; then
            echo "   FAIL $spec $src"
            printf '%s\n' "$out" | grep -E "$WARN_RE" | head -8
            bad=$((bad + 1))
        fi
    done
    if [ "$bad" -eq 0 ]; then
        echo "   ok   $spec（$count 个文件）"
    else
        failed=$((failed + bad))
    fi
done

echo
if [ "$failed" -eq 0 ]; then
    echo "== C++ 模式全部干净（${#SPECS[@]} 个 cb 构建 × ./cb test，$(( ${#SWEEP_SPECS[@]} )) 轮源码扫描）=="
else
    echo "== 有 $failed 处失败 =="
fi
echo
echo "临时文件在 $BUILD_DIR（可自行删除）"
exit "$failed"
