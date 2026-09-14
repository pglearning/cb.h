#!/usr/bin/env python3
"""从 cb.h 生成两份文档，顺便验证"没有没人用的公共接口"。

    docs/api.md           —— API 速查表：每个公共接口的名字、种类、所属节
    docs/api-coverage.md  —— 覆盖对照表：每个接口在哪个示例/测试里被真正用到

为什么要用脚本而不是手写
------------------------
手写的 API 列表一定会烂掉：这轮就实测到了两个例子——上一版 api.md 漏了
39 个结构体类型（多行 `} CB_String_View;` 老脚本没识别）和 7 个后来新增的接口，
而老脚本又顺手把内部的 CB__Alloc_Record 也列了进去。脚本 + `--check` 模式
能把这些偏差变成 CI 里的一条失败。

判定规则（全部自动，没有硬编码名单）
------------------------------------
1. 公共接口 = cb.h 里 cb_/CB_ 开头、非 cb__/CB__ 的
     - #define NAME / #define NAME(...)          -> 宏 / 宏
     - CBDEF ... NAME(...)                       -> 函数
     - typedef ... (*NAME)(...)                  -> 函数指针类型
     - typedef struct/union/enum {...} NAME;     -> 类型（含多行写法）
     - typedef struct X Y;                       -> 类型
2. 按 cb.h 的节横幅（100 个 '/' + "// 节名"；含文件末尾由本脚本生成的别名区）分组；同名只算一次，
   归属第一次出现的节。
3. 用法搜索范围：examples/*.c tests/*.c bench/*.c cb.c，词边界匹配
   （避免 CB_MAP_EMPTY 命中 CB_MAP_EMPTYISH）。
4. 上面四处没用到的，再看 cb.h 自己用没用（排除它自己的声明行）：
     - 对象式 #define（常量/编译期开关） -> 配置常量
     - 类型                              -> 公共类型
     - 其余（函数/函数式宏）             -> 内部辅助
5. 四处没用、cb.h 自己也不用 -> "无使用者"，退出码 1。这是回归防线：
   以后新增了没人用的接口，CI 直接失败。

用法：
    tools/gen-docs.py            # 重新生成两份文档
    tools/gen-docs.py --check    # 只检查是否最新 + 有无无使用者接口（CI 用）
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "cb.h")
API_DOC = os.path.join(ROOT, "docs", "api.md")
COVERAGE_DOC = os.path.join(ROOT, "docs", "api-coverage.md")

# 去前缀别名区：这些名字去掉前缀后会和标准库 / POSIX / C++ 撞车，不生成别名
STRIP_PREFIX_SKIP = {
    # 规则：凡是标准库 / POSIX / 系统头里已经存在的名字，一律保留 cb_ 前缀，
    # 去前缀宏不碰它们。名单不是凭印象列的，是扫出来的两组数据：
    #   1) libc + libm + libpthread 的导出符号，以及 mingw 导入库（msvcrt/kernel32/
    #      user32/advapi32）的符号
    #   2) clang / mingw 用 -dM -E 导出的全部系统宏（Linux 2867 个 + mingw 29358 个）
    # 扫描结果只有三个额外命中：rotl64、rotr64（glibc 符号）、ERROR（mingw 宏）。
    "cb_log": "libm 的 log()",
    "cb_rename": "stdio.h 的 rename()",
    "cb_glob": "POSIX glob.h 的 glob()",
    "cb_rotl64": "C23 <stdbit.h> / glibc 的 rotl64",
    "cb_rotr64": "C23 <stdbit.h> / glibc 的 rotr64",
    "CB_PATH_MAX": "limits.h 的 PATH_MAX（CB_PATH_MAX 本身就是从它兜底来的）",
    "CB_ERROR": "mingw <wingdi.h> 的 #define ERROR 0",
    "cb_min": "Windows 的 min 宏、C++ std::min",
    "cb_max": "Windows 的 max 宏、C++ std::max",
    "cb_clamp": "C++ std::clamp",
    "cb_swap": "C++ std::swap",
}
# 这些名字在某些平台上已经被系统头定义为宏，别名必须用 #ifndef 包起来，否则重定义
STRIP_PREFIX_GUARDED = {
    # mingw 的 <wingdi.h> 里有 `#define ERROR 0`（GDI 错误码）
    "CB_ERROR": "_WIN32 的 <wingdi.h> 有 #define ERROR 0",
}
STRIP_BEGIN = "// >>> CB_STRIP_PREFIX 生成区开始（tools/gen-docs.py 生成，不要手改）"
STRIP_END = "// <<< CB_STRIP_PREFIX 生成区结束"

USAGE_DIRS = ["examples", "tests", "bench"]
USAGE_EXTRA = ["cb.c"]
BANNER = "/" * 100

KIND_LABEL = {
    "enum": "常量",
    "func": "函数",
    "macro_fn": "宏",
    "macro_obj": "宏",
    "fnptr": "类型",
    "type": "类型",
}


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def sections(lines):
    """返回 [(节名, 代码起始行, 结束行)]，按文件顺序。"""
    marks = []
    for i, line in enumerate(lines):
        if (
            line.startswith(BANNER)
            and i + 1 < len(lines)
            and lines[i + 1].startswith("// ")
            and i + 2 < len(lines)
            and lines[i + 2].startswith(BANNER)
        ):
            marks.append((i, lines[i + 1][3:].strip()))
    return [
        (
            name,
            start + 3,
            marks[idx + 1][0] if idx + 1 < len(marks) else len(lines),
        )
        for idx, (start, name) in enumerate(marks)
    ]


def is_public(name):
    return (
        (name.startswith("cb_") or name.startswith("CB_"))
        and not name.startswith(("cb__", "CB__"))
    )


def declarations_in_line(line):
    """这一行声明了哪些公共接口：{名字: 种类}。

    函数用非贪婪 .*? 取"第一个被 ( 跟随的标识符"，所以
        CBDEF void cb_log(...) CB_PRINTF_FORMAT(1, 2);
    解析出的是 cb_log，不是行尾的属性宏。
    """
    s = line.strip()
    out = {}

    def take(name, kind):
        if is_public(name):
            out[name] = kind

    m = re.match(r"^#\s*define\s+([A-Za-z_]\w*)(\()?", s)
    if m:
        take(m.group(1), "macro_fn" if m.group(2) else "macro_obj")
        return out

    m = re.match(r"^CBDEF\b.*?\b([A-Za-z_]\w*)\s*\(", s)
    if m:
        take(m.group(1), "func")
        return out

    m = re.match(r"^typedef\b.*?\(\s*\*\s*([A-Za-z_]\w*)\s*\)", s)
    if m:
        take(m.group(1), "fnptr")
        return out

    m = re.match(r"^typedef\b.*?}\s*([A-Za-z_]\w*)\s*;", s)
    if m:
        take(m.group(1), "type")
        return out

    m = re.match(r"^typedef\s+(?:struct|union|enum)\s+\w+\s+([A-Za-z_]\w*)\s*;", s)
    if m:
        take(m.group(1), "type")
        return out

    return out


def candidates(chunk):
    """一段 cb.h 代码里的公共接口 {名字: 种类}，保持出现顺序。

    多行 typedef（typedef struct { ... } CB_Foo;）需要向前找收尾行。
    """
    found = {}
    i = 0
    while i < len(chunk):
        for n, k in declarations_in_line(chunk[i]).items():
            found.setdefault(n, k)

        s = chunk[i].strip()
        if re.match(r"^typedef\s+(?:struct|union|enum)\b", s) and "}" not in s:
            is_enum = s.startswith("typedef enum")
            for j in range(i + 1, min(i + 400, len(chunk))):
                m = re.match(r"^}\s*([A-Za-z_]\w*)\s*;", chunk[j].strip())
                if m:
                    if is_public(m.group(1)):
                        found.setdefault(m.group(1), "type")
                    i = j
                    break
                if is_enum:
                    # 枚常量：CB_INFO, / CB_FILE_ERROR = -1, —— 用户会直接写 INFO / FILE_REGULAR
                    em = re.match(r"^([A-Za-z_]\w*)\s*(?:=[^,]*)?,?\s*$", chunk[j].strip())
                    if em and is_public(em.group(1)):
                        found.setdefault(em.group(1), "enum")
                if chunk[j].strip().startswith("typedef"):
                    break
        i += 1
    return found


def extract(header_text):
    """返回 [(名字, 种类, 节名)]，按 cb.h 里第一次出现的顺序。

    先剔掉 CB_STRIP_PREFIX 生成区：那一整块也是 #define，会被自己的解析器当成
    新接口读回来（CB_STRIP_PREFIX_GUARD_ 就踩过），于是别名区会自我增殖。
    """
    if STRIP_BEGIN in header_text:
        start = header_text.index(STRIP_BEGIN)
        end = header_text.index(STRIP_END, start) + len(STRIP_END)
        header_text = header_text[:start] + header_text[end:]
    lines = header_text.splitlines()
    order = []
    meta = {}
    for sec_name, start, end in sections(lines):
        for name, kind in candidates(lines[start:end]).items():
            if name in meta:
                continue
            meta[name] = (kind, sec_name)
            order.append(name)
    return [(n, meta[n][0], meta[n][1]) for n in order]


def strip_declarations(header_text, name):
    """删掉该接口自己的声明/定义行，剩下的才算"被 cb.h 自己用到"。"""
    return "\n".join(
        line
        for line in header_text.splitlines()
        if name not in declarations_in_line(line)
    )


def analyze():
    header_text = read(HEADER)
    ifaces = extract(header_text)
    if not ifaces:
        print("错误：没能从 cb.h 解析出任何接口，规则可能已失效", file=sys.stderr)
        sys.exit(2)

    usage_files = []
    for d in USAGE_DIRS:
        full = os.path.join(ROOT, d)
        if os.path.isdir(full):
            usage_files += [
                os.path.join(full, f)
                for f in sorted(os.listdir(full))
                if f.endswith((".c", ".h"))
            ]
    usage_files += [os.path.join(ROOT, f) for f in USAGE_EXTRA if os.path.isfile(os.path.join(ROOT, f))]
    usage = {f: read(f) for f in usage_files}

    used, config, types_only, internal, unused = [], [], [], [], []
    for name, kind, sec in ifaces:
        pat = re.compile(r"\b" + re.escape(name) + r"\b")
        where = [os.path.relpath(f, ROOT) for f in usage_files if pat.search(usage[f])]
        if where:
            used.append((name, kind, sec, where))
            continue
        if not pat.search(strip_declarations(header_text, name)):
            unused.append((name, kind, sec))
        elif kind in ("macro_obj", "enum"):
            # 编译期常量 / 枚举常量：只被库自己读，不需要调用者“使用”
            config.append((name, kind, sec))
        elif kind in ("type", "fnptr"):
            types_only.append((name, kind, sec))
        else:
            internal.append((name, kind, sec))

    return {
        "header_text": header_text,
        "ifaces": ifaces,
        "used": used,
        "config": config,
        "types_only": types_only,
        "internal": internal,
        "unused": unused,
    }


def gen_api_doc(r):
    lines = r["header_text"].splitlines()
    out = [
        "# cb.h API 速查表",
        "> 由 `tools/gen-docs.py` 从 `cb.h` 的声明里自动提取（只列公共接口，"
        "内部 `cb__*` 已排除；同名只列一次）。重新生成：`tools/gen-docs.py`",
        ">",
        "> - 每个接口的详细语义写在 `cb.h` 里它的声明上方",
        "> - 分模块的使用说明与可运行示例见 [guide.md](guide.md)",
        "> - 每个接口在哪个示例/测试里被真正用到见 [api-coverage.md](api-coverage.md)",
        "> - 与上游 nob.h 的差异见 [nob-comparison.md](nob-comparison.md)",
        "",
    ]
    kinds = [k for _n, k, _s in r["ifaces"]]
    n_func = sum(1 for k in kinds if k == "func")
    n_macro = sum(1 for k in kinds if k.startswith("macro") or k == "enum")
    n_type = sum(1 for k in kinds if k in ("type", "fnptr"))
    out.append(
        f"共 **{len(r['ifaces'])}** 个公共接口：函数 {n_func} 个、宏 {n_macro} 个、类型 {n_type} 个。"
    )
    out.append("")

    by_sec = {}
    for name, kind, sec in r["ifaces"]:
        by_sec.setdefault(sec, []).append((name, kind))

    emitted = set()
    for sec_name, _s, _e in sections(lines):
        items = by_sec.get(sec_name)
        if not items or sec_name in emitted:
            continue
        emitted.add(sec_name)
        out.append(f"### {sec_name}")
        for name, kind in items:
            if KIND_LABEL[kind] == "函数":
                out.append(f"- `{name}()`")
            else:
                out.append(f"- `{name}`（{KIND_LABEL[kind]}）")
        out.append("")

    return "\n".join(out) + "\n"


def gen_coverage_doc(r):
    stats = [
        ("在示例/测试/基准/cb.c 里被实际使用", len(r["used"])),
        ("配置常量（编译期开关，只被库自己读）", len(r["config"])),
        ("公共类型（作为其它接口的参数/字段）", len(r["types_only"])),
        ("内部辅助（函数/宏，只被库自己调）", len(r["internal"])),
        ("没有任何使用者的公共接口", len(r["unused"])),
        ("合计", len(r["ifaces"])),
    ]

    out = [
        "# cb.h 公共接口覆盖对照表",
        "> 由 `tools/gen-docs.py` 自动生成：对每个公共接口，"
        "在 `examples/`、`tests/`、`bench/` 与 `cb.c` 里搜索它的**实际使用**。",
        "> 所以这张表是可验证的，不是声称。重新生成：`tools/gen-docs.py`",
        "",
        "| 类别 | 数量 |",
        "|---|---|",
    ]
    out += [f"| {k} | **{v}** |" for k, v in stats]
    out += ["", "---", "", "## 被实际使用的接口", ""]
    out += ["| 节 | 接口 | 在哪里被用到 |", "|---|---|---|"]
    for name, _kind, sec, where in r["used"]:
        out.append(f"| {sec} | `{name}` | {'、'.join('`' + w + '`' for w in where)} |")
    out.append("")

    def listing(title, items, blurb):
        if not items:
            return
        out.extend(["---", "", f"## {title}（{len(items)} 个）", "", blurb, ""])
        for name, _kind, sec in items:
            out.append(f"- `{name}`（{sec}）")
        out.append("")

    listing(
        "配置常量",
        r["config"],
        "编译期开关或常量，只被 cb.h 自己读取，不需要外部调用：",
    )
    listing(
        "公共类型",
        r["types_only"],
        "作为其它公共接口的参数或字段出现，不需要在示例里单独调用：",
    )
    listing(
        "内部辅助",
        r["internal"],
        "技术上可见，但只有 cb.h 自己在用，不构成对外接口：",
    )
    listing("⚠ 无使用者", r["unused"], "要么给它写测试/示例，要么它就是死代码：")

    return "\n".join(out) + "\n"


def gen_strip_prefix_block(ifaces):
    """生成 CB_STRIP_PREFIX 别名区文本（不含文件末尾的换行）。"""
    by_sec = {}
    for name, _kind, sec in ifaces:
        if name in STRIP_PREFIX_SKIP:
            continue
        by_sec.setdefault(sec, []).append(name)

    skip_lines = "\n".join(
        f"//   {n[3:]:<16} 撞 {why}" for n, why in sorted(STRIP_PREFIX_SKIP.items())
    )
    out = [
        BANNER,
        "// CB_STRIP_PREFIX：去前缀别名",
        BANNER,
        "// 默认不生效。在 #include \"cb.h\" 之前 #define CB_STRIP_PREFIX，就能写",
        "// temp_sprintf / String_View，而不是 cb_temp_sprintf / CB_String_View。",
        "//",
        "// 为什么放在文件最末尾：实现区用的都是带前缀的名字，别名区如果放在前面会互相干扰。",
        "// 自带 include guard，header-only 模式下被多个 .c 各自 include 也只展开一次。",
        "//",
        "// 刻意不生成别名的名字（去掉前缀会与标准库 / POSIX / 系统库 / 第三方库撞车）：",
        skip_lines,
        "//",
        "// 名单不是凭印象列的：拿 libc/libm/libpthread、mingw 导入库的真实导出符号，",
        "// 以及 clang/mingw -dM -E 的全部系统宏扫出来比对，命中的就是上面这些名字。",
        "//",
        "// 本区由 tools/gen-docs.py 生成，不要手改；./cb docs --check 会校验它与 cb.h 是否同步。",
        "#ifndef CB_STRIP_PREFIX_GUARD_",
        "#define CB_STRIP_PREFIX_GUARD_",
        "#  ifdef CB_STRIP_PREFIX",
    ]
    for sec, names in by_sec.items():
        out.append(f"// ---- {sec} ----")
        for n in sorted(names):
            out.append(f"#    define {n[3:]} {n}")
    out += [
        "#  endif // CB_STRIP_PREFIX",
        "#endif // CB_STRIP_PREFIX_GUARD_",
    ]
    return "\n".join(out)


def sync_strip_prefix(header_text, block, check_only):
    """把别名区写入 cb.h 的标记区；返回 (新文本, 是否已是最新)。"""
    marked = f"{STRIP_BEGIN}\n{block}\n{STRIP_END}\n"
    if STRIP_BEGIN in header_text:
        start = header_text.index(STRIP_BEGIN)
        end = header_text.index(STRIP_END, start) + len(STRIP_END) + 1
        current = header_text[start:end]
        return header_text[:start] + marked + header_text[end:], current == marked
    # 首次生成：追加到文件末尾
    text = header_text.rstrip("\n") + "\n\n" + marked
    return text, False


def main():
    check_only = "--check" in sys.argv
    r = analyze()

    for name, _kind, sec in r["unused"]:
        print(f"无使用者：{name}（{sec}）", file=sys.stderr)

    # 去前缀别名区是写在 cb.h 里的第三份生成物
    new_header, strip_fresh = sync_strip_prefix(
        r["header_text"], gen_strip_prefix_block(r["ifaces"]), check_only
    )

    docs = [(API_DOC, gen_api_doc(r)), (COVERAGE_DOC, gen_coverage_doc(r))]

    stale = []
    for path, text in docs:
        if not os.path.isfile(path) or read(path) != text:
            stale.append(os.path.relpath(path, ROOT))
    if not strip_fresh:
        stale.append("cb.h（CB_STRIP_PREFIX 别名区）")

    print(f"cb.h 公共接口 {len(r['ifaces'])} 个：")
    for k, v in [
        ("被实际使用", len(r["used"])),
        ("配置常量", len(r["config"])),
        ("公共类型", len(r["types_only"])),
        ("内部辅助", len(r["internal"])),
        ("无使用者", len(r["unused"])),
    ]:
        print(f"  {v:4d}  {k}")

    if check_only:
        if strip_fresh:
            print(f"  CB_STRIP_PREFIX 别名区是最新的")
        if stale:
            print("\n以下文档已过期，请运行 tools/gen-docs.py：" + "、".join(stale), file=sys.stderr)
        else:
            print("\n文档都是最新的")
        return 1 if (stale or r["unused"]) else 0

    if not strip_fresh:
        with open(HEADER, "w", encoding="utf-8") as f:
            f.write(new_header)
        print("已更新 cb.h 的 CB_STRIP_PREFIX 别名区")
    for path, text in docs:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"已写入 {os.path.relpath(path, ROOT)}")
    return 1 if r["unused"] else 0


if __name__ == "__main__":
    sys.exit(main())
