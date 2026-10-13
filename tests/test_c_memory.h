#ifndef JSONBENCH_TEST_C_MEMORY_H
#define JSONBENCH_TEST_C_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

// Parser diagnostics and strdup seeds use C/POSIX allocation. Their C++ RAII
// owner delegates release to the corresponding C allocator through this bridge.
void jsonbench_test_release_diagnostic(char* memory);

#ifdef __cplusplus
}
#endif

#endif
