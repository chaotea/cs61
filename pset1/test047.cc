#include "m61.hh"
#include <cstdio>
#include <cassert>
#include <cstring>
// Test realloc of a pointer that wasn't allocated yet

int main() {
    char* ptr = (char*) malloc(32);
    m61_realloc(ptr - 32, 32, "test047.cc", 9);
    m61_print_statistics();
}

//! MEMORY BUG???: invalid realloc of pointer ???, pointer wasn't allocated yet
//! ???
