#include "m61.hh"
#include <cstdio>
#include <cassert>
#include <cstring>
// Realloc a pointer to a smaller size

int main() {
    void* ptr = malloc(32);
    m61_realloc(ptr, 16, "test048.cc", 9);
    m61_print_statistics();
}

//! MEMORY BUG???: invalid realloc of pointer ???, memory block of size ??? cannot be reallocated to ??? bytes
//! ???
