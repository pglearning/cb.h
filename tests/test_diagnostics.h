// 测试专用的告警压制。放在 tests/ 下而不是 cb.h 里：这是本仓库测试的写法约定，
// 不该替库的使用者关掉他们自己的告警。
//
// 背景：测试按项目风格用 {0} 零初始化（刻意不用库里那个零值宏）。C 下 {0} 不触发任何
// 告警，C++ 下 -Wmissing-field-initializers 会为每个没写到的成员各报一条。而 CI 会把
// 全部测试当 C++ 编译（cb.c 用 C++ 编译器构建时，它给自己那套编译选项里带 -x c++），
// 所以这两个告警必须压掉。
//
// 压制方式和 cb.c 顶部完全一致，三层守卫缺一不可：
//   1. #ifdef __cplusplus —— C 下不需要；
//   2. 按编译器分支 —— gcc 见到 `#pragma clang diagnostic` 会报 -Wunknown-pragmas
//      （-Wall 里带着它），clang 见到 `#pragma GCC diagnostic` 也一样；
//   3. __has_warning 探测 —— "-Wmissing-designated-field-initializers" 是较新 clang
//      才有的 warning group，老 clang 不认识，直接写会反过来报
//      "unknown warning group '-Wmissing-designated-field-initializers', ignored"。
//
// 每个 tests/*.c 都在 #include "cb.h" 之前包含本文件——包括暂时还没有 {0} 的那些：
// 以后新增一个 {0} 不该顺带引入一个只在 C++ 分支里才看得见的告警。
#ifdef __cplusplus
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#if defined(__has_warning)
#if __has_warning("-Wmissing-designated-field-initializers")
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif // __has_warning("-Wmissing-designated-field-initializers")
#endif // defined(__has_warning)
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif // __clang__ / __GNUC__
#endif // __cplusplus
