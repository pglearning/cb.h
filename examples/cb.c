#define CB_IMPLEMENTATION
#define CB_ENABLE_ECHO
#define CB_STRIP_PREFIX
#define CB_ALLOC_TRACK
#include "cb.h"

// Dependens: glslangValidator wayland-scanner

#define OUTPUT_FOLDER "./bin/"
// #define BUILD_FOLDER "./build/"

#ifdef _WIN32
#define DYNAMIC_LIB_SUFFIX ".dll"
#else
#define DYNAMIC_LIB_SUFFIX ".so"
#endif /* ifdef _WIN32 */

// Gen wayland protocols
#define WAYLAND_GEN_FOLDER "cage/include/wayland/"

static const char* const wayland_protocols[] = {
    "wayland", "viewporter", "xdg-shell",
    "idle-inhibit-unstable-v1", "pointer-constraints-unstable-v1",
    "relative-pointer-unstable-v1", "fractional-scale-v1",
    "xdg-activation-v1", "xdg-decoration-unstable-v1",
    NULL,
};

bool gen_wayland_symbol(void)
{
    if (!mkdir_if_not_exists(WAYLAND_GEN_FOLDER)) return false;

    for (size_t i = 0; wayland_protocols[i] != NULL; ++i) {
        const char* name = wayland_protocols[i];

        const char* wayland_dep_list = temp_sprintf("%s%s%s", "./cage/include/glfw/deps/wayland/", name, ".xml");
        const char* header = temp_sprintf("%s%s%s", WAYLAND_GEN_FOLDER, name, "-client-protocol.h");
        const char* code   = temp_sprintf("%s%s%s", WAYLAND_GEN_FOLDER, name, "-client-protocol-code.h");

        if (file_exists(header) && file_exists(code) &&
            file_mtime(header) >= file_mtime(wayland_dep_list) &&
            file_mtime(code) >= file_mtime(wayland_dep_list)) {
            continue;
        }

        Cmd hcmd = ZERO;
        cmd_append(&hcmd, "wayland-scanner", "client-header", wayland_dep_list, header);
        bool hok = cmd_run(&hcmd);
        CB_FREE(hcmd.items);
        if (!hok) {
            cb_log(CB_ERROR, "client-header failed: %s", name);
            return false;
        }

        Cmd ccmd = ZERO;
        cmd_append(&ccmd, "wayland-scanner", "private-code", wayland_dep_list, code);
        bool cok = cmd_run(&ccmd);
        CB_FREE(ccmd.items);
        if (!cok) {
            cb_log(CB_ERROR, "private-code failed: %s", name);
            return false;
        }
    }
    return true;
}
// -- Gen wayland protocols

// Baking shader into cage.so
// *.vert / *.frag -> glslangValidator -> *.spv -> *_spv.h
//      glslangValidator -V --target-env vulkan1.2
//      -I./assets/shaders/include/glsl \
//      -o cage/include/shaders/Renderer2D.vert.spv \
//      cage/include/shaders/Renderer2D.vert
// TODO (i dont think we need this): <name>.glsl -> <name>.vert/<name>.frag
#define SHADER_GEN_TARGET "vulkan1.2"
#define SHADER_GEN_FOLDER "cage/include/shaders/"
#define SHADER_ASSETS_FOLDER "./assets/shaders/"

typedef struct {
    const char* name;
    const char* include_path;
    const char* type;
} Shader_Asset;

static const Shader_Asset shader_assets[] = {
    { "renderer2d", SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "renderer2d", SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    // ⑤-c1：场景合成（全屏三角形 + 合成 + 色调映射）
    { "scenecomposite", SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "scenecomposite", SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    // ⑤-c2（W-d2）：几何通道（网格源 → 几何目标）
    { "geometry", SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "geometry", SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    // ⑤-d（W-f1）：前深度 / 天空盒 / 网格地面
    { "predepth", SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "predepth", SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "skybox", SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "skybox", SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "grid", SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "grid", SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    // ⑤-d（方向光级联阴影）：只写深度的阴影通道
    { "dirshadowmap", SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "dirshadowmap", SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    // ⑤-d（余下渲染特性，第一批）：**计算**通道（`.comp`）。
    // NOTE: `type` 就是文件扩展名 —— 运行期的 `assets/shaders/<name>.comp` 由后端现编
    //       （`vulkan_shader.c` 见到 `<name>.comp` 就只认 COMPUTE 阶段），这里登记是让
    //       `cb` 构建期把它们**先编一遍**（glslangValidator 的语法/反射错误在构建期就暴露，
    //       而不是留到运行时第一帧）。
    // ★ `shader_assets[0]` 仍然是 Renderer2D（生成的头文件名取自 [0].name），顺序未动。
    { "hzb", SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    { "lightculling", SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    { "gtao", SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    // ★ `name` 同时是**文件主干**与**生成的 C 符号前缀**：符号侧会把 `-` 换成 `_`
    //   （`cb_sv_to_temp_upper`）⇒ 数组名是 `CAGE_GTAO_DENOISE_COMP_SPV`，而
    //   `cage__vulkan` 运行时也是把 `-` 归一成 `_` 去找（照抄 ⑥ 的既有口径）。
    { "gtao_denoise", SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    // ★ ⑤-d2 IBL 链（R-2 的三份计算着色器；顺序 = Equirectangular → Irradiance → MipFilter）
    { "equirectangulartocubemap", SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    { "environmentirradiance",    SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    { "environmentmipfilter",     SHADER_ASSETS_FOLDER "include/glsl", "comp" },

    // ① 2D 渲染的余下三条管线（圆 / 线段 / MSDF 文本）—— 上游 `Renderer2D_{Circle,Line,Text}.glsl`
    { "renderer2d_circle", SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "renderer2d_circle", SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "renderer2d_line",   SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "renderer2d_line",   SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "renderer2d_text",   SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "renderer2d_text",   SHADER_ASSETS_FOLDER "include/glsl", "frag" },

    // ② 编辑器辅助绘制：线框 / 选中几何 / 纯纹理通道（各含非动画 + `_Anim` 骨骼变体）
    { "wireframe",               SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "wireframe",               SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "wireframe_anim",          SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "wireframe_anim",          SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "selectedgeometry",        SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "selectedgeometry",        SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "selectedgeometry_anim",   SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "selectedgeometry_anim",   SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "texturepass",             SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "texturepass",             SHADER_ASSETS_FOLDER "include/glsl", "frag" },

    // ③ Jump Flood 三件套（编辑器选中描边：初始化 → 跳步传播 → 描边合成）
    { "jumpflood_init",          SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "jumpflood_init",          SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "jumpflood_pass",          SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "jumpflood_pass",          SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "jumpflood_composite",     SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "jumpflood_composite",     SHADER_ASSETS_FOLDER "include/glsl", "frag" },

    // ④ 阴影通道的余下变体（聚光灯 + 方向光/前深度的骨骼动画版）
    { "spotshadowmap",           SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "spotshadowmap",           SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "spotshadowmap_anim",      SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "spotshadowmap_anim",      SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "dirshadowmap_anim",       SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "dirshadowmap_anim",       SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "predepth_anim",           SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "predepth_anim",           SHADER_ASSETS_FOLDER "include/glsl", "frag" },

    // ⑤ 天空模型（Perez/Preetham 解析天空 → cube 的存储镜像；**纯计算、无顶点阶段**）
    { "preethamsky",             SHADER_ASSETS_FOLDER "include/glsl", "comp" },

    // ⑥ 后处理链余下的计算通道（可分离高斯预卷积 / bloom 四模式 / 线性重采样 / 可见度预积分 / SSR）
    { "pre_convolution",         SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    { "bloom",                   SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    { "linearsample",            SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    { "linearsampleuint",        SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    { "pre_integration",         SHADER_ASSETS_FOLDER "include/glsl", "comp" },
    { "ssr",                     SHADER_ASSETS_FOLDER "include/glsl", "comp" },

    // ⑦ 后处理链余下的光栅通道（景深 / 边缘检测 / SSR 合成 / AO 合成）
    { "dof",                     SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "dof",                     SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "edgedetection",           SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "edgedetection",           SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "ssr_composite",           SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "ssr_composite",           SHADER_ASSETS_FOLDER "include/glsl", "frag" },
    { "ao_composite",            SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "ao_composite",            SHADER_ASSETS_FOLDER "include/glsl", "frag" },

    // ⑧ 上游的示例/调试着色器 `shader.glsl`（法线可视化样例；确有内容 ⇒ 一并补齐）
    { "shader",                  SHADER_ASSETS_FOLDER "include/glsl", "vert" },
    { "shader",                  SHADER_ASSETS_FOLDER "include/glsl", "frag" },
};
#define shader_assets_count ARRAY_LEN(shader_assets)

bool gen_shader_symbol(void)
{
    if (!mkdir_if_not_exists(SHADER_GEN_FOLDER)) return false;

    const char* header = temp_sprintf(SHADER_GEN_FOLDER "%s_spv.h", shader_assets[0].name);

    if (file_exists(header)) {
        int64_t header_time = file_mtime(header);
        bool need_update = false;

        for (size_t i = 0; i < shader_assets_count; ++i) {
            const char* src = temp_sprintf(SHADER_ASSETS_FOLDER "%s.%s", shader_assets[i].name, shader_assets[i].type);
            if (!file_exists(src) || header_time < file_mtime(src)) { need_update = true; break; }
        }

        if (!need_update) {
            cb_log(CB_INFO, "skip shaders (up to date)");
            return true;
        }
    }

    String_Builder output = ZERO;
    sb_appendf(&output, "// This file auto generat by cb.c (source: %s)\n", SHADER_ASSETS_FOLDER);
    sb_appendf(&output, "#pragma once\n\n#include <stdint.h>\n");

    for (size_t i = 0; i < shader_assets_count; ++i) {
        const char* name = shader_assets[i].name;
        const char* type = shader_assets[i].type;
        const char* src = temp_sprintf(SHADER_ASSETS_FOLDER "%s.%s", name, type);
        const char* dst = temp_sprintf(SHADER_GEN_FOLDER "%s.%s.spv", name, type);

        // 1. .vert/.frag -> .spv
        Cmd scmd = ZERO;
        cmd_append(&scmd, "glslangValidator", "-V", "--target-env", SHADER_GEN_TARGET);
        cmd_append(&scmd, temp_sprintf("%s%s", "-I", shader_assets[i].include_path));
        cmd_append(&scmd, "-I" SHADER_ASSETS_FOLDER "include/common");
        cmd_append(&scmd, "-o", dst);
        cmd_append(&scmd, src);

        bool sok = cmd_run(&scmd);
        CB_FREE(scmd.items);
        if (!sok) {
           cb_log(CB_ERROR, "glslangValidator gen failed: %s (need: Vulkan SDK / glslang-tools)", src);
           return false;
        }

        // 2. .spv -> uint32 数组，追加进 output
        String_Builder spv = ZERO;
        if (!read_entire_file(dst, &spv)) return false;
        if (spv.count == 0 || (spv.count % 4) != 0) {
            cb_log(CB_ERROR, "bad spir-v: %s (%d bytes)", dst, (int)spv.count);
            CB_FREE(spv.items);
            return false;
        }
        const uint32_t* words      = (const uint32_t*)spv.items;
        size_t          word_count = spv.count / 4;

        const char* upper_name = cb_sv_to_temp_upper(sv_from_cstr(name));
        const char* upper_type = cb_sv_to_temp_upper(sv_from_cstr(type));
        sb_appendf(&output, "\nstatic const uint32_t %s[] = {", temp_sprintf("CAGE_%s_%s_SPV", upper_name, upper_type));
        for (size_t i = 0; i < word_count; ++i) {
            if ((i % 8) == 0) sb_appendf(&output, "\n    ");
            sb_appendf(&output, "0x%08x, ", words[i]);
        }
        sb_appendf(&output, "\n};\n");

        CB_FREE(spv.items);
    }

    write_entire_file(header, output.items, output.count);
    CB_FREE(output.items);
    return true;
}
// -- Baking shader into cage.so

typedef struct {
    bool should_run;
    // NOTE: Only support dynamic lib and exec file
    // Cant used clang build to static lib, must build .o then 'ar rcs <out.a> <*.o ...>'
    bool is_dynamic_lib;
    const char* output;
    const char* run_arg;
    const char* source_dir;
    const char* const * extra_flags;
    const char* const* link_flags;
    const char* const * include_dirs;
} Project;

static const Project projects[] = {
    {
        .output = "cage",
        .is_dynamic_lib = true,
        .should_run = false,
        .run_arg = NULL,
        .source_dir = "./cage/",
        .include_dirs = (const char* const[]){
            ".",
            "cage/include",
            "cage/include/glfw/src",
            SHADER_GEN_FOLDER,
            WAYLAND_GEN_FOLDER,
            NULL
        },
        .extra_flags = (const char* const[]){
            "-D_DEFAULT_SOURCE",    // for -std=c99 glibc
            // "-D_GLFW_X11",
            "-D_GLFW_WAYLAND",
            "-DHAVE_MEMFD_CREATE",  // GLFW Wayland memfd_create for script engine
            "-DCAGE_DEBUG=1",
            "-DCAGE_DEBUG_DEEP=1",
            // "-DCAGE_ANTI_CHEAT_ENABLED=0",   // disable anti-cheat
            NULL
        },
        .link_flags = (const char* const[]){
            "-lrt", "-lm", "-ldl", "-lpthread",
            // "-lX11", "-lXrandr", "-lXinerama", "-lXcursor", "-lXi", "-lXext",
            "-lwayland-client", "-lwayland-cursor", "-lwayland-egl", "-lxkbcommon",
            "-lvulkan",
            "-ldav1d",    // video decode
            "-Wl,-soname,cage.so",  // same as filename, DT_NEEDED find the file by name.
            NULL
        },
    },
    {
        .output = "editor",
        .should_run = false,
        .run_arg = NULL,
        .source_dir = "./editor/",
        .include_dirs = (const char* const[]){
            ".",
            NULL
        },
        .extra_flags = (const char* const[]){
            "-Wl,--no-undefined",
            "-DCAGE_DEBUG=1",
            "-DCAGE_DEBUG_DEEP=1",
            // "-DCAGE_ANTI_CHEAT_ENABLED=0",   // disable anti-cheat
            NULL
        },
        .link_flags = (const char* const[]){
            // NOTE: for .a
            // "-Wl,--export-dynamic",
            // "-Wl,--whole-archive",
            // "-Wl,--no-whole-archive",

            "-lrt", "-lm", "-ldl", "-lpthread",
            // "-lX11", "-lXrandr", "-lXinerama", "-lXcursor", "-lXi", "-lXext",
            "-lwayland-client", "-lwayland-cursor", "-lwayland-egl", "-lxkbcommon",
            "-lvulkan",
            "./bin/cage.so",    // DT_NEEDED
            "-Wl,--enable-new-dtags",
            "-Wl,-rpath,$ORIGIN",
            NULL
        },
    },
};
#define projects_count ARRAY_LEN(projects)

// NOTE: This should collect all under project directory .c file.
bool collect_sources(Walk_Entry entry)
{
    if (entry.type != FILE_REGULAR) return true;
    const char* ext = temp_file_ext(entry.path);
    if (ext == NULL) return true;
    if (strcmp(ext, ".c") != 0) return true;

    da_append((File_Paths*)entry.data, temp_strdup(entry.path));
    cb_log(CB_INFO, "%s", entry.path);
    return true;
}

bool project_run(const Project* project, const char* bin_path)
{
    Cmd cmd = ZERO;
    const char* stdout_path = temp_sprintf("%s%s.stdout.txt", OUTPUT_FOLDER, project->output);

    cmd_append(&cmd, bin_path);
    if (project->run_arg != NULL) cmd_append(&cmd, project->run_arg);

    // bool ok = cmd_run(&cmd, .stdout_path = stdout_path);
    // if (ok) cb_log(INFO, "run output -> %s", stdout_path);
    bool ok = cmd_run(&cmd);

    CB_FREE(cmd.items);
    return ok;
}

bool project_build_and_run(const Project* project)
{
    bool result = true;
    Cmd cmd = ZERO;
    File_Paths src_files = ZERO;

    const char* src_dir = project->source_dir;
    char* bin_path = temp_sprintf("%s%s", OUTPUT_FOLDER, project->output);
    if (project->is_dynamic_lib)
        bin_path = temp_sprintf("%s%s", bin_path, DYNAMIC_LIB_SUFFIX);

    if (!mkdir_if_not_exists(OUTPUT_FOLDER)) return_defer(false);
    if (!walk_dir(src_dir, collect_sources, .data = &src_files)) return_defer(false);
    if (src_files.count == 0) {
        cb_log(CB_ERROR, "no .c src_files under %s", src_dir);
        return_defer(false);
    }

    cc(&cmd);
    cmd_append(&cmd, "-std=c99");
    // cmd_append(&cmd, "-Wall", "-Wextra", "-std=c99");

    if (project->is_dynamic_lib) {
        cmd_append(&cmd, "-fPIC", "-shared");
    }

    for (size_t i = 0; project->extra_flags != NULL && project->extra_flags[i] != NULL; ++i) {
        cmd_append(&cmd, project->extra_flags[i]);
    }
    for (size_t i = 0; project->include_dirs!= NULL && project->include_dirs[i] != NULL; ++i) {
        cmd_append(&cmd, temp_sprintf("-I%s", project->include_dirs[i]));
    }
    for (size_t i = 0; project->link_flags != NULL && project->link_flags[i] != NULL; ++i) {
        cmd_append(&cmd, project->link_flags[i]);
    }

    cmd_append(&cmd, "-o", bin_path);

    for (size_t i = 0; i < src_files.count; ++i) {
        cmd_append(&cmd, src_files.items[i]);
    }

    if (!cmd_run(&cmd, .dont_reset = false)) return_defer(false);

    if (project->should_run && !project_run(project, bin_path)) return_defer(false);

defer:
    CB_FREE(src_files.items);
    CB_FREE(cmd.items);
    return result;
}

// Gen UI charset (form all .c and .h file)
#define UI_CHARSET_HEADER "cage/include/cage_ui_charset.h"
#define UI_CHARSET_LINE_CHARS 100

typedef struct {
    uint32_t* items;
    size_t    count;
    size_t    capacity;
} Charset_Cps;

bool collect_charset_source(Walk_Entry entry)
{
    if (entry.type != FILE_REGULAR) return true;
    const char* ext = temp_file_ext(entry.path);
    if (ext == NULL) return true;
    if (strcmp(ext, ".c") != 0 && strcmp(ext, ".h") != 0) return true;
    da_append((File_Paths*)entry.data, temp_strdup(entry.path));
    return true;
}

// UTF-8 → 码点。非法/截断序列返回 0 且只前进 1 字节（绝不吞掉后面的字符）。
static uint32_t charset_utf8_decode(const unsigned char* s, size_t n, size_t* advance)
{
    const unsigned char b = s[0];
    if (b < 0x80) { *advance = 1; return (uint32_t)b; }
    if ((b & 0xE0) == 0xC0 && n >= 2) {
        *advance = 2;
        return ((uint32_t)(b & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
    }
    if ((b & 0xF0) == 0xE0 && n >= 3) {
        *advance = 3;
        return ((uint32_t)(b & 0x0Fu) << 12) | ((uint32_t)(s[1] & 0x3Fu) << 6) | (uint32_t)(s[2] & 0x3Fu);
    }
    if ((b & 0xF8) == 0xF0 && n >= 4) {
        *advance = 4;
        return ((uint32_t)(b & 0x07u) << 18) | ((uint32_t)(s[1] & 0x3Fu) << 12) |
               ((uint32_t)(s[2] & 0x3Fu) << 6) | (uint32_t)(s[3] & 0x3Fu);
    }
    *advance = 1;
    return 0;
}

static int charset_cp_compare(const void* a, const void* b)
{
    const uint32_t ca  = *(const uint32_t*)a;
    const uint32_t cb_ = *(const uint32_t*)b;
    return (ca > cb_) - (ca < cb_);
}

bool gen_ui_charset_symbol(void)
{
    if (!mkdir_if_not_exists("cage/include")) return false;

    File_Paths files = ZERO;
    if (!walk_dir("./editor/", collect_charset_source, .data = &files)) return false;
    if (!walk_dir("./cage/",   collect_charset_source, .data = &files)) return false;

    Charset_Cps cps = ZERO;
    for (size_t i = 0; i < files.count; ++i) {
        String_Builder src = ZERO;
        if (!read_entire_file(files.items[i], &src)) { CB_FREE(cps.items); return false; }
        const unsigned char* p = (const unsigned char*)src.items;
        bool inside = false;
        for (size_t k = 0; k < src.count; ) {
            if (p[k] == '"') { inside = !inside; k += 1; continue; }
            size_t         advance = 1;
            const uint32_t cp      = charset_utf8_decode(p + k, src.count - k, &advance);
            if (inside && cp >= 0x80) da_append(&cps, cp);
            k += advance;
        }
        CB_FREE(src.items);
    }
    if (cps.count == 0) {
        cb_log(CB_ERROR, "ui charset: 一个码点都没扫到（根目录 ./editor/ ./cage/）");
        return false;
    }

    qsort(cps.items, cps.count, sizeof(cps.items[0]), charset_cp_compare);
    size_t unique = 0;
    for (size_t i = 0; i < cps.count; ++i) {
        if (unique == 0 || cps.items[i] != cps.items[unique - 1]) cps.items[unique++] = cps.items[i];
    }

    String_Builder out = ZERO;
    sb_appendf(&out, "// ★ 自动生成 —— 不要手改。\n");
    sb_appendf(&out, "// 供 `cage_font_create_charset()` 把这些字形烘进 UI 字体图集（引擎原本只烘 ASCII 32..126，\n");
    sb_appendf(&out, "// 中文界面因此整片空白）。\n");
    sb_appendf(&out, "// 统计：%d 个码点。字符串按 %d 字符一行拼接。\n", (int)unique, UI_CHARSET_LINE_CHARS);
    sb_appendf(&out, "#ifndef CAGE_UI_CHARSET_H\n");
    sb_appendf(&out, "#define CAGE_UI_CHARSET_H\n\n");
    sb_appendf(&out, "static const char CAGE_UI_CHARSET[] =\n");
    for (size_t i = 0; i < unique; ) {
        const size_t line_count = (unique - i < UI_CHARSET_LINE_CHARS) ? (unique - i) : UI_CHARSET_LINE_CHARS;
        sb_appendf(&out, "    \"");
        for (size_t k = 0; k < line_count; ++k) {
            const uint32_t cp = cps.items[i + k];
            char           buf[4];
            size_t         n = 0;
            if (cp < 0x800) {
                buf[n++] = (char)(0xC0u | (cp >> 6));
                buf[n++] = (char)(0x80u | (cp & 0x3Fu));
            } else if (cp < 0x10000) {
                buf[n++] = (char)(0xE0u | (cp >> 12));
                buf[n++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                buf[n++] = (char)(0x80u | (cp & 0x3Fu));
            } else {
                buf[n++] = (char)(0xF0u | (cp >> 18));
                buf[n++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
                buf[n++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                buf[n++] = (char)(0x80u | (cp & 0x3Fu));
            }
            sb_appendf(&out, "%.*s", (int)n, buf);
        }
        sb_appendf(&out, "%s\n", (i + line_count == unique) ? "\";" : "\"");
        i += line_count;
    }
    sb_appendf(&out, "\n#endif  // CAGE_UI_CHARSET_H\n");

    // ★ 内容相同就不写：否则每次构建都刷新 mtime ⇒ 下游（editor.c 等）全部重编。
    if (file_exists(UI_CHARSET_HEADER)) {
        String_Builder old = ZERO;
        if (!read_entire_file(UI_CHARSET_HEADER, &old)) { CB_FREE(out.items); CB_FREE(cps.items); return false; }
        const bool same = (old.count == out.count) && (memcmp(old.items, out.items, out.count) == 0);
        CB_FREE(old.items);
        if (same) {
            cb_log(CB_INFO, "skip ui charset (up to date): %d 个码点", (int)unique);
            CB_FREE(out.items);
            CB_FREE(cps.items);
            return true;
        }
    }

    if (!write_entire_file(UI_CHARSET_HEADER, out.items, out.count)) {
        cb_log(CB_ERROR, "ui charset: 写不了 %s", UI_CHARSET_HEADER);
        CB_FREE(out.items);
        CB_FREE(cps.items);
        return false;
    }
    cb_log(CB_INFO, "ui charset: %d 个码点 -> %s", (int)unique, UI_CHARSET_HEADER);
    CB_FREE(out.items);
    CB_FREE(cps.items);
    return true;
}
// -- Gen UI charset


int main(int argc, char** argv)
{
    minimal_log_level = INFO;
    set_log_handler(default_log_handler);

    SELF_REBUILD(argc, argv, "cb.h");

    // Gen UI charset header (editor needs it; clean tree must build)
    if (!gen_ui_charset_symbol()) return 1;
    // Gen wayland protocols (glfw at wayland need this)
    if (!gen_wayland_symbol()) return 1;
    // Baking shader into cage.so
    // Cage  way:   Used glslangValidator (Vulkan SDK / glslang-tools) Barking binary to .h file -> cage.so
    // Hazel way:   shaderc & glslang need link libstdc++ (static ~2-4 MB)
    //              SPIRV-Cross  SPIR-V -> source / reflection, editor prop need?
    if (!gen_shader_symbol()) return 1;

    // Build & Run projcet
    for (size_t i = 0; i < projects_count; ++i) {
        Arena_Mark mark = temp_save();
        bool ok = project_build_and_run(&projects[i]);
        temp_rewind(mark);
        if (!ok) return 1;
    }
    return 0;
}
