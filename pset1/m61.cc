#define M61_DISABLE 1
#define METADATA_LIMIT 20000000
#define MAGIC_NUMBER 42
#include "m61.hh"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cinttypes>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
using namespace std;

// Initialize stats struct
struct m61_statistics _stats {0, 0, 0, 0, 0, 0, 0, 0};

// Metadata tracking active sizes
struct metadata {
    size_t size;
    const char* file;
    long line;
    bool active;
    metadata() {
        size = 0;
        file = nullptr;
        line = 0;
        active = true;
    }
    metadata(size_t _size, const char* _file, long _line, bool _active) {
        this->size = _size;
        this->file = _file;
        this->line = _line;
        this->active = _active;
    }
};

unordered_map<uintptr_t, metadata> allocated_pointers;
unordered_map<string, size_t> heavy_hitters;
queue<uintptr_t> allocated_pointers_q;


/// m61_malloc(sz, file, line)
///    Return a pointer to `sz` bytes of newly-allocated dynamic memory.
///    The memory is not initialized. If `sz == 0`, then m61_malloc must
///    return a unique, newly-allocated pointer value. The allocation
///    request was at location `file`:`line`.

void* m61_malloc(size_t sz, const char* file, long line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    // Your code here.

    // If size is too large, return nullptr
    if (sz >= (size_t) - 1){
        _stats.nfail++;
        _stats.fail_size += sz;
        return nullptr;
    }

    // Allocate
    void* ptr = base_malloc(sz + 1);

    // Check if allocation failed
    if (ptr == NULL) {
        _stats.nfail++;
        _stats.fail_size += sz;
        return ptr;
    }

    // Metadata for detecting boundary write error
    char* bound = (char*) ((uintptr_t)ptr + sz);
    *bound = MAGIC_NUMBER;

    // Update stats
    metadata m(sz, file, line, true);
    allocated_pointers[(uintptr_t) ptr] = m;
    allocated_pointers_q.push((uintptr_t) ptr);
    while (allocated_pointers_q.size() >= METADATA_LIMIT) {
        allocated_pointers.erase(allocated_pointers_q.front());
        allocated_pointers_q.pop();
    }
    _stats.nactive++;
    _stats.ntotal++;
    _stats.total_size += sz;
    _stats.active_size += sz;
    if ((uintptr_t) ptr < _stats.heap_min || _stats.heap_min == 0) _stats.heap_min = (uintptr_t) ptr;
    if ((uintptr_t) ptr + sz - 1 > _stats.heap_max || _stats.heap_max == 0) _stats.heap_max = (uintptr_t) ptr + sz - 1;

    // Update heavy hitters
    string hh = file;
    hh.append(":");
    hh.append(to_string(line));
    if (heavy_hitters.find(hh) != heavy_hitters.end()) {
        heavy_hitters[hh] += sz;
    } else {
        heavy_hitters[hh] = sz;
    }

    return ptr;
}


/// m61_free(ptr, file, line)
///    Free the memory space pointed to by `ptr`, which must have been
///    returned by a previous call to m61_malloc. If `ptr == NULL`,
///    does nothing. The free was called at location `file`:`line`.

void m61_free(void* ptr, const char* file, long line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    // Your code here.

    // Check if null pointer
    if (ptr == NULL) {
        return;
    }

    // Check if pointer exists in allocated_pointers metadata
    if (allocated_pointers.find((uintptr_t) ptr) != allocated_pointers.end()) {
        // Check if double free
        if (!allocated_pointers[(uintptr_t) ptr].active) {
            fprintf(stderr, "MEMORY BUG: %s:%ld: invalid free of pointer %p, double free\n", file, line, ptr);
        }

        // Check if boundary write error
        char* bound = (char*) ((uintptr_t)ptr + allocated_pointers[(uintptr_t) ptr].size);
        if (*bound != MAGIC_NUMBER) {
            fprintf(stderr, "MEMORY BUG: %s:%ld: detected wild write during free of pointer %p\n", file, line, ptr);
            abort();
        }
        
        // Free
        base_free(ptr);

        // Update stats
        --_stats.nactive;
        _stats.active_size -= allocated_pointers[(uintptr_t) ptr].size;
        allocated_pointers[(uintptr_t) ptr].active = false;
    } else {
        for (auto it = allocated_pointers.begin(); it != allocated_pointers.end(); it++) {
            if ((uintptr_t) ptr > it->first && (uintptr_t) ptr <= it->first + it->second.size) {
                fprintf(stderr, "MEMORY BUG: %s:%ld: invalid free of pointer %p, not allocated\n", file, line, ptr);
                fprintf(stderr, "\t%s:%ld: %p is %ld bytes inside a %ld byte region allocated here\n", file, it->second.line, ptr, (uintptr_t) ptr - it->first, it->second.size);
                return;
            }
        }
        fprintf(stderr, "MEMORY BUG: %s:%ld: invalid free of pointer %p, not in heap\n", file, line, ptr);
    }
}


/// m61_calloc(nmemb, sz, file, line)
///    Return a pointer to newly-allocated dynamic memory big enough to
///    hold an array of `nmemb` elements of `sz` bytes each. If `sz == 0`,
///    then must return a unique, newly-allocated pointer value. Returned
///    memory should be initialized to zero. The allocation request was at
///    location `file`:`line`.

void* m61_calloc(size_t nmemb, size_t sz, const char* file, long line) {
    // Your code here (to fix test019).
    if ((size_t) -1 / sz < nmemb) {
        _stats.nfail++;
        _stats.fail_size += nmemb * sz;
        return nullptr;
    }
    void* ptr = m61_malloc(nmemb * sz, file, line);
    if (ptr) {
        memset(ptr, 0, nmemb * sz);
    }
    return ptr;
}


/// m61_get_statistics(stats)
///    Store the current memory statistics in `*stats`.

void m61_get_statistics(m61_statistics* stats) {
    // Stub: set all statistics to enormous numbers
    memset(stats, 255, sizeof(m61_statistics));
    // Your code here.
    *stats = _stats;
}


/// m61_print_statistics()
///    Print the current memory statistics.

void m61_print_statistics() {
    m61_statistics stats;
    m61_get_statistics(&stats);

    printf("alloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("alloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}


/// m61_print_leak_report()
///    Print a report of all currently-active allocated blocks of dynamic
///    memory.

void m61_print_leak_report() {
    // Your code here.
    for (auto it = allocated_pointers.begin(); it != allocated_pointers.end(); it++) {
        if (it->second.active) {
            printf("LEAK CHECK: %s:%ld: allocated object %p with size %lu\n", it->second.file, it->second.line, (void*) it->first, it->second.size);
        }
    }
}


/// hh_cmp()
///    Helper function to compare sizes of heavy hitters
bool hh_cmp(pair<string, size_t> &a, pair<string, size_t> &b) {
    return a.second > b.second;
}


/// m61_print_heavy_hitter_report()
///    Print a report of heavily-used allocation locations.

void m61_print_heavy_hitter_report() {
    // Your heavy-hitters code here
    vector<pair<string, size_t>> hh_vector;
    for (auto it : heavy_hitters) {
        hh_vector.push_back(it);
    }
    sort(hh_vector.begin(), hh_vector.end(), hh_cmp);
    for (auto it = hh_vector.begin(); it != hh_vector.end(); it++) {
        double percentage = (double) it->second / _stats.total_size * 100;
        if (percentage >= 20) {
            printf("HEAVY HITTER: %s: %lu bytes (~%.1f%%)\n", it->first.substr(2).c_str(), it->second, percentage);
        }
    }
}