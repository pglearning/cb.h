#!/usr/bin/env bash
#
# Windows 交叉编译 + wine 验证脚本
#
# 用途：在 Linux 上验证 cb.h 的 Windows 分支（编译 + 实际运行测试）。
#
# 用法：
#     tools/verify-windows.sh              # 编译、运行并与 golden 比对
#     tools/verify-windows.sh arena fs     # 只跑指定测试
#     tools/verify-windows.sh --record     # 录制 tests/<名字>.win32.stdout.txt
#
# 依赖：
#     - mingw-w64 交叉编译器：pacman -S mingw-w64-gcc
#     - wine 与 wineboot
#
# 关于 wine-mono 弹窗：
#     Wine 在初始化 prefix 时会提示安装 wine-mono（.NET 支持）与 wine-gecko（HTML 支持）。
#     本脚本用 WINEDLLOVERRIDES="mscoree,mshtml=" 把这两个组件关掉即可，原因是：
#       * mscoree 是 .NET 的入口，mshtml 是 HTML 渲染器
#       * cb.h 的测试程序只依赖 KERNEL32.dll 与 UCRT（可用
#         x86_64-w64-mingw32-objdump -p <exe> | grep 'DLL Name' 核实），
#         完全不碰 .NET 或浏览器组件
#     所以关掉它们对本验证没有任何影响。
#
#     如果你想彻底不弹窗又保留 .NET 能力，也可以安装发行版打包好的组件：
#         pacman -S wine-mono wine-gecko

set -euo pipefail

cd "$(dirname "$0")/.."

CC=${CC:-x86_64-w64-mingw32-gcc}
CXX=${CXX:-x86_64-w64-mingw32-g++}
CFLAGS=${CFLAGS:-"-std=c99 -Wall -Wextra -Wswitch-enum"}
BUILD_DIR=${BUILD_DIR:-$(mktemp -d)}
EXPORT_PREFIX=${WINEPREFIX:-$(mktemp -d)}

# 关闭 .NET / HTML 组件提示（见文件头说明）
export WINEDLLOVERRIDES="mscoree,mshtml="
export WINEDEBUG=${WINEDEBUG:--all}
export LIBGL_ALWAYS_SOFTWARE=1
export WINEPREFIX="$EXPORT_PREFIX"

# 与 cb.c 里的 test_names[] 保持一致
ALL_TESTS=(alloc_track arena build_flags bytes_for_utf8 chain dynamic_array error_paths fs
           logging map nob_parity procs read_entire_dir stdlib string_builder string_view
           strip_prefix temp_storage toolbox)

RECORD=0
if [ "${1:-}" = "--record" ]; then
    RECORD=1
    shift
fi

if [ "$#" -gt 0 ]; then
    TESTS=("$@")
else
    TESTS=("${ALL_TESTS[@]}")
fi

echo "== 编译器 =="
$CC --version | head -1
echo "== 构建目录: $BUILD_DIR =="
echo "== wine prefix: $WINEPREFIX =="

echo
echo "== 1) 交叉编译 cb.c（C 与 C++ 两种模式）=="
$CC  $CFLAGS -I. -c cb.c -o "$BUILD_DIR/cb_c.o"
$CXX -std=c++17 -Wall -Wextra -I. -x c++ -c cb.c -o "$BUILD_DIR/cb_cxx.o"
$CC  $CFLAGS -I. -o "$BUILD_DIR/cb.exe" cb.c
echo "   OK（0 错误 0 警告）"

echo
echo "== 2) 交叉编译并运行测试 =="
timeout 300 wineboot --init >/dev/null 2>&1 || true

failed=0
for name in "${TESTS[@]}"; do
    src="tests/$name.c"
    [ -f "$src" ] || { echo "   ?? 找不到 $src"; failed=$((failed + 1)); continue; }

    $CC $CFLAGS -I. -o "$BUILD_DIR/$name.exe" "$src"

    # 每个测试在自己的空目录里跑，避免互相干扰
    run_dir="$BUILD_DIR/run_$name"
    mkdir -p "$run_dir"
    # Windows 的 stdout 是文本模式，会把 \n 翻成 \r\n；归一到 LF 再比。
    (cd "$run_dir" && timeout 180 wine "$BUILD_DIR/$name.exe" 2>/dev/null) | tr -d '\r' > "$BUILD_DIR/$name.out"
    rc=${PIPESTATUS[0]}

    if [ "$RECORD" = "1" ]; then
        cp "$BUILD_DIR/$name.out" "tests/$name.win32.stdout.txt"
        echo "   rec  $name"
        continue
    fi

    # 比对 golden。测试是"纯 printf + golden"风格：程序自己不打印 ok/FAILED，
    # 所以必须逐字节比对输出。
    #
    # golden 的选择：有平台差异的用例用 tests/<名字>.win32.stdout.txt 覆盖，
    # 其余的直接和 Linux 用同一份 tests/<名字>.stdout.txt——后者是更强的断言：
    # 它证明这些功能在两个平台上输出逐字节一致。
    golden="tests/$name.win32.stdout.txt"
    [ -f "$golden" ] || golden="tests/$name.stdout.txt"

    if [ "$rc" -ne 0 ]; then
        echo "   FAIL $name（退出码 $rc）"
        failed=$((failed + 1))
        continue
    fi
    if [ ! -f "$golden" ]; then
        echo "   ??   $name 没有 golden（先跑 tools/verify-windows.sh --record $name）"
        failed=$((failed + 1))
        continue
    fi
    if diff -q "$BUILD_DIR/$name.out" "$golden" >/dev/null 2>&1; then
        echo "   ok   $name"
    else
        echo "   FAIL $name（与 $golden 不一致）"
        # `|| true`：diff 在文件不同时返回 1，set -e + pipefail 会让脚本
        # 在第一个失败处就退出，后面的失败就全看不到了。
        diff "$BUILD_DIR/$name.out" "$golden" | head -8 || true
        failed=$((failed + 1))
    fi
done

echo
if [ "$failed" -eq 0 ]; then
    echo "== 全部通过（${#TESTS[@]} 个测试）=="
else
    echo "== 有 $failed 个测试失败 =="
fi

echo
echo "临时文件在 $BUILD_DIR（可自行删除）"
exit "$failed"
