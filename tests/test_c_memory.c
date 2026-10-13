#include "test_c_memory.h"

#include <stdlib.h>

void jsonbench_test_release_diagnostic(char* memory) {
    free(memory);
}
