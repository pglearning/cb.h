#!/usr/bin/env bash
#
# 示例检查脚本：编译并真正运行 examples/ 下的每个构建脚本
#
# 用途：CI 的 examples 步骤是唯一会运行示例的地方，本地没有等价命令——"
# 本地只跑 ./cb test"就漏掉了构建脚本里写错的路径、命令行和链接顺序。
# 示例现在是纯构建脚本：它们会 walk 演示项目、调用编译器、把产物写进 bin/，
# 所以"跑得起来"本身就是对 walk_dir / Cmd / 构建 API 的端到端验证。
#
# 用法：
#     tools/verify-examples.sh                        # 两遍：默认链接、模拟 CI 的 --as-needed
#     tools/verify-examples.sh --quick                # 只跑默认那一遍
#     CC=clang tools/verify-examples.sh               # 换编译器
#     CB_LANG=c++ CC=clang++ tools/verify-examples.sh # 按 C++ 编译示例（CI 的 C++ leg 用这条）
#
# 语言模式：CB_LANG=c（默认）用 -std=c99 -D_POSIX_C_SOURCE=200112L；
#           CB_LANG=c++ 用 -x c++ -std=c++20。这里选 c++20 而不是 c++17：示例里的
#           ".field = ..." 选项语法到 C++20 才进标准，C++17 下只是编译器扩展。
#
# 为什么必须跑两遍
# ----------------
# Ubuntu 的 gcc/clang 默认带 --as-needed，本地编译器（自建 gcc / 发行版 clang）通常不带。
# 于是"库排在源码前面"这类错误在本地全过、到 CI 一起挂：链接器处理到那个库时还没有任何
# 未定义符号，直接把库丢掉，随后在源码处报 undefined reference。多项目示例正是靠链接
# ./bin/core.so 才暴露这一类问题。
# 第二遍用一个再加 -Wl,--as-needed 的 cc 垫片放在 PATH 最前面，把这层默认差异复现出来
# （cb.h 发出的是裸 "cc"，所以垫片对示例内部的构建步骤同样生效）。
#
# 两遍都会先删掉示例产物 bin/：构建脚本有 needs_rebuild 判断，不删就等于第二遍什么都没做。

set -euo pipefail

cd "$(dirname "$0")/.."

CC=${CC:-cc}
CB_LANG=${CB_LANG:-c}
BUILD_DIR=${BUILD_DIR:-$(mktemp -d)}
QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1

case "$CB_LANG" in
    c)              LANG_FLAGS=(-std=c99 -D_POSIX_C_SOURCE=200112L) ;;
    c++|cpp|cxx)    LANG_FLAGS=(-x c++ -std=c++20) ;;
    *)              echo "CB_LANG 只支持 c 或 c++（现在是 '$CB_LANG'）" >&2; exit 2 ;;
esac

# examples/ 自带一份 cb.h，这样整个目录被单独复制走也能编译（软链接在 Windows 上会退化成
# 文本文件，cp -r 复制后还会变成悬空链接）。这里守住"两份必须一致"：改了根目录的 cb.h 就得
# 同步复制过去，否则示例跑的是旧头文件，而 CI 会在这里直接失败。
if ! cmp -s cb.h examples/cb.h; then
    echo "FAIL examples/cb.h 与根目录 cb.h 不一致" >&2
    echo "     修法：cp cb.h examples/cb.h" >&2
    exit 1
fi
echo "== examples/cb.h 与 cb.h 一致 =="

echo "== 编译器 =="
$CC --version | head -1
echo "== 语言: $CB_LANG（${LANG_FLAGS[*]}）=="
echo "== 构建目录: $BUILD_DIR =="

# 垫片：名字就叫 cc，因为示例内部 cb_cc 发出的正是裸 "cc"（跟 nob.h 的 nob_cc 一样）。
#   shim/     cc -> $CC                    让内部构建也用 $CC（CC=g++ 时就真的全程 g++）
#   shim_asn/ cc -> $CC -Wl,--as-needed    再补上 CI 默认的 --as-needed
# 两个都以 $CC 的绝对路径 exec，不会递归到自己。
CC_PATH=$(command -v "$CC")
SHIM_DIR="$BUILD_DIR/shim"
SHIM_AS_NEEDED_DIR="$BUILD_DIR/shim_as_needed"
mkdir -p "$SHIM_DIR" "$SHIM_AS_NEEDED_DIR"
printf '#!/bin/sh\nexec %s "$@"\n' "$CC_PATH" > "$SHIM_DIR/cc"
printf '#!/bin/sh\nexec %s -Wl,--as-needed "$@"\n' "$CC_PATH" > "$SHIM_AS_NEEDED_DIR/cc"
chmod +x "$SHIM_DIR/cc" "$SHIM_AS_NEEDED_DIR/cc"

failed=0

# 一遍 = 用固定的 PATH 编译并运行全部示例。
# 示例自己会在 build/examples/ 下建目录，所以每遍之前先清掉上一次的产物。
run_pass() {
    local label="$1" path_prefix="$2" pass=0 fail=0

    echo
    echo "== $label =="
    for src in examples/*.c; do
        name=$(basename "$src" .c)
        # 示例产物都落在仓库根的 bin/：删掉它，needs_rebuild 才会真的重新编译+链接
        rm -rf bin

        if ! out=$(PATH="$path_prefix$PATH" $CC "${LANG_FLAGS[@]}" \
                       -Wall -Wextra -I. "$src" -o "$BUILD_DIR/$name" 2>&1); then
            echo "   FAIL $name（编译失败）"
            printf '%s\n' "$out" | head -8
            fail=$((fail + 1))
            continue
        fi

        if ! out=$(PATH="$path_prefix$PATH" timeout 300 "$BUILD_DIR/$name" 2>&1); then
            echo "   FAIL $name（运行失败）"
            printf '%s\n' "$out" | tail -12
            fail=$((fail + 1))
            continue
        fi

        echo "   ok   $name"
        pass=$((pass + 1))
    done

    echo "   -> $pass 通过 / $fail 失败"
    failed=$((failed + fail))
}

run_pass "默认链接（示例内部构建也走 $CC）" "$SHIM_DIR/"
if [ "$QUICK" -eq 0 ]; then
    run_pass "模拟 CI 的 --as-needed（Ubuntu 的 gcc/clang 默认带上它）" "$SHIM_AS_NEEDED_DIR/"
fi

echo
if [ "$failed" -eq 0 ]; then
    echo "== 示例全部编译并运行成功 =="
else
    echo "== 有 $failed 个示例失败 =="
fi

echo
echo "临时文件在 $BUILD_DIR（可自行删除）"
exit "$failed"
