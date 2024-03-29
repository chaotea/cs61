#define M61_DISABLE 1
#define BOUND_FREES 8
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
#include <list>
#include <unordered_map>
#include <algorithm>
#include <random>
using namespace std;


// Define a metadata struct to track active allocations and their sizes
struct metadata {
    size_t size;
    const char* file;
    long line;
    metadata() {
        size = 0;
        file = nullptr;
        line = 0;
    }
    metadata(size_t _size, const char* _file, long _line) {
        this->size = _size;
        this->file = _file;
        this->line = _line;
    }
};

// Initialize stats, metadata map, and heavy hitters
struct m61_statistics _stats {0, 0, 0, 0, 0, 0, 0, 0};
unordered_map<uintptr_t, metadata> metadata_map;
unordered_map<string, pair<size_t, size_t>> heavy_hitters;
list<uintptr_t> frees;
size_t hh_size = 0;
size_t hh_total = 0;


/// m61_malloc(sz, file, line)
///    Return a pointer to `sz` bytes of newly-allocated dynamic memory.
///    The memory is not initialized. If `sz == 0`, then m61_malloc must
///    return a unique, newly-allocated pointer value. The allocation
///    request was at location `file`:`line`.

void* m61_malloc(size_t sz, const char* file, long line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings

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
    metadata m(sz, file, line);
    metadata_map[(uintptr_t) ptr] = m;
    _stats.nactive++;
    _stats.ntotal++;
    _stats.total_size += sz;
    _stats.active_size += sz;
    if ((uintptr_t) ptr < _stats.heap_min || _stats.heap_min == 0) _stats.heap_min = (uintptr_t) ptr;
    if ((uintptr_t) ptr + sz - 1 > _stats.heap_max || _stats.heap_max == 0) _stats.heap_max = (uintptr_t) ptr + sz - 1;

    // Update heavy hitters using random sampling
    string hh = file;
    hh.append(":");
    hh.append(to_string(line));
    if (rand() % 100 < 10) {
        if (heavy_hitters.find(hh) != heavy_hitters.end()) {
            size_t temp_size = heavy_hitters[hh].first;
            size_t temp_count = heavy_hitters[hh].second;
            heavy_hitters[hh] = make_pair(temp_size + sz, temp_count + 1);
        } else {
            heavy_hitters[hh] = make_pair(sz, 1);
        }
        hh_size += sz;  // update total size
        hh_total += 1;  // update total number
    }

    return ptr;
}


/// m61_free(ptr, file, line)
///    Free the memory space pointed to by `ptr`, which must have been
///    returned by a previous call to m61_malloc. If `ptr == NULL`,
///    does nothing. The free was called at location `file`:`line`.

void m61_free(void* ptr, const char* file, long line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings

    // Check if null pointer
    if (ptr == NULL) {
        return;
    }

    // Check if pointer exists in metadata_map metadata
    if (metadata_map.find((uintptr_t) ptr) != metadata_map.end()) {

        // Check if double free
        if (metadata_map[(uintptr_t) ptr].size == 0) {
            fprintf(stderr, "MEMORY BUG: %s:%ld: invalid free of pointer %p, double free\n", file, line, ptr);
            abort();
        }

        // Check if boundary write error
        char* bound = (char*) ((uintptr_t)ptr + metadata_map[(uintptr_t) ptr].size);
        if (*bound != MAGIC_NUMBER) {
            fprintf(stderr, "MEMORY BUG: %s:%ld: detected wild write during free of pointer %p\n", file, line, ptr);
            abort();
        }
        
        // Free
        base_free(ptr);

        // Update stats
        --_stats.nactive;
        _stats.active_size -= metadata_map[(uintptr_t) ptr].size;
        metadata_map[(uintptr_t) ptr].size = 0;
    } else {

	    // Check if not in heap
        if ((uintptr_t) ptr < _stats.heap_min || (uintptr_t) ptr > _stats.heap_max) {
            fprintf(stderr, "MEMORY BUG: %s:%ld: invalid free of pointer %p, not in heap\n", file, line, ptr);
            abort();
        }

	    // Check if pointer points to invalid region
        for (auto it = metadata_map.begin(); it != metadata_map.end(); it++) {
            if ((uintptr_t) ptr > it->first && (uintptr_t) ptr <= it->first + it->second.size) {
                fprintf(stderr, "MEMORY BUG: %s:%ld: invalid free of pointer %p, not allocated\n", file, line, ptr);
                fprintf(stderr, "\t%s:%ld: %p is %ld bytes inside a %ld byte region allocated here\n", file, it->second.line, ptr, (uintptr_t) ptr - it->first, it->second.size);
                abort();
            }
        }

	    // Pointer not allocated (it doesn't exist in metadata)
        fprintf(stderr, "MEMORY BUG: %s:%ld: invalid free of pointer %p, not allocated\n", file, line, ptr);
        abort();
    }

    if (frees.size() > BOUND_FREES) {
        metadata_map.erase(frees.front());
        frees.pop_front();
        frees.push_back((uintptr_t) ptr);
    }
}


/// m61_calloc(nmemb, sz, file, line)
///    Return a pointer to newly-allocated dynamic memory big enough to
///    hold an array of `nmemb` elements of `sz` bytes each. If `sz == 0`,
///    then must return a unique, newly-allocated pointer value. Returned
///    memory should be initialized to zero. The allocation request was at
///    location `file`:`line`.

void* m61_calloc(size_t nmemb, size_t sz, const char* file, long line) {
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

/// m61_realloc(ptr, sz, file, line)
///    Reallocate the dynamic memory pointed to by `ptr` to hold at least
///    `sz` bytes, returning a pointer to the new block. If `ptr` is
///    `nullptr`, behaves like `m61_malloc(sz, file, line)`. If `sz` is 0,
///    behaves like `m61_free(ptr, file, line)`. The allocation request
///    was at location `file`:`line`.

void* m61_realloc(void* ptr, size_t sz, const char* file, long line) {
    if (ptr == NULL) {
        return m61_malloc(sz, file, line);
    }
    if (metadata_map.find((uintptr_t) ptr) == metadata_map.end()) {
        fprintf(stderr, "MEMORY BUG: %s:%ld: invalid realloc of pointer %p, pointer wasn't allocated yet\n", file, line, ptr);
        abort();
    }
    if (sz == 0) {
        m61_free(ptr, file, line);
        return nullptr;
    }
    if (sz < metadata_map[(uintptr_t) ptr].size) {
        fprintf(stderr, "MEMORY BUG: %s:%ld: invalid realloc of pointer %p, memory block of size %ld cannot be reallocated to %ld bytes\n", file, line, ptr, metadata_map[(uintptr_t) ptr].size, sz);
        abort();
    }
    void* realloc_ptr = m61_malloc(sz, file, line);
    memcpy(realloc_ptr, ptr, sz);
    m61_free(ptr, file, line);
    return realloc_ptr;
}


/// m61_get_statistics(stats)
///    Store the current memory statistics in `*stats`.

void m61_get_statistics(m61_statistics* stats) {
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
    for (auto it = metadata_map.begin(); it != metadata_map.end(); it++) {
        if (it->second.size != 0) {
            printf("LEAK CHECK: %s:%ld: allocated object %p with size %lu\n", it->second.file, it->second.line, (void*) it->first, it->second.size);
        }
    }
}


/// hh_size_cmp()
///    Helper function to compare heavy hitter sizes
bool hh_size_cmp(pair<string, pair<size_t, size_t>> &a, pair<string, pair<size_t, size_t>> &b) {
    return a.second.first > b.second.first;
}

/// hh_freq_cmp()
///    Helper function to compare heavy hitter frequencies
bool hh_freq_cmp(pair<string, pair<size_t, size_t>> &a, pair<string, pair<size_t, size_t>> &b) {
    return a.second.second > b.second.second;
}

/// m61_print_heavy_hitter_report()
///    Print a report of heavily-used allocation locations.

void m61_print_heavy_hitter_report() {
    vector<pair<string, pair<size_t, size_t> > > hh_vector;
    for (auto it : heavy_hitters) {
        pair<string, pair<size_t, size_t>> hh = make_pair(it.first, make_pair(it.second.first, it.second.second));
        hh_vector.push_back(hh);
    }
    sort(hh_vector.begin(), hh_vector.end(), hh_size_cmp);
    printf("-----by heaviness-----\n");
    for (auto it = hh_vector.begin(); it != hh_vector.end(); it++) {
        double percentage = (double) it->second.first / hh_size * 100;
        if (percentage >= 20) {
            printf("HEAVY HITTER: %s: %lu bytes (~%.1f%%)\n", it->first.substr(2).c_str(), it->second.first, percentage);
        }
    }
    sort(hh_vector.begin(), hh_vector.end(), hh_freq_cmp);
    printf("-----by frequency-----\n");
    for (auto it = hh_vector.begin(); it != hh_vector.end(); it++) {
        double percentage = (double) it->second.second / hh_total * 100;
        if (percentage >= 20) {
            printf("HEAVY HITTER: %s: %lu allocations (~%.1f%%)\n", it->first.substr(2).c_str(), it->second.second, percentage);
        }
    }
}