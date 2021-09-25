#include "m61.hh"
#include <cstdio>
#include <cassert>
#include <cstring>
// Realloc a pointer

int main() {
    void* ptr = malloc(32);
    m61_realloc(ptr, 32, "test046.cc", 9);
    m61_print_statistics();
}

//! alloc count: active          1   total          2   fail          0
//! alloc size:  active         32   total         64   fail          0
