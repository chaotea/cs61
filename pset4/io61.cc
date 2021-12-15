#include "io61.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <climits>
#include <cerrno>
#include <algorithm>


// io61.cc
//    YOUR CODE HERE!


// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

struct io61_file {
    int fd;
    int mode;
    static constexpr off_t bufsize = 4096;
    unsigned char cbuf[bufsize];
    off_t tag;
    off_t end_tag;
    off_t pos_tag;
};


// io61_fdopen(fd, mode)
//    Return a new io61_file for file descriptor `fd`. `mode` is
//    either O_RDONLY for a read-only file or O_WRONLY for a
//    write-only file. You need not support read/write files.

io61_file* io61_fdopen(int fd, int mode) {
    assert(fd >= 0);
    io61_file* f = new io61_file;
    f->fd = fd;
    f->mode = mode;
    return f;
}


// io61_close(f)
//    Close the io61_file `f` and release all its resources.

int io61_close(io61_file* f) {
    io61_flush(f);
    int r = close(f->fd);
    delete f;
    return r;
}


// io61_fill(f)
//    Fill the read cache

int io61_fill(io61_file* f) {
    // Reset the cache
    f->tag = f->pos_tag = f->end_tag;

    // Fill the cache with up to (bufsize) characters
    int n = read(f->fd, f->cbuf, f->bufsize);
    if (n >= 0) {
        f->end_tag = f->tag + n;
        return n;
    } else {
        return -1;
    }
}


// io61_read(f, buf, sz)
//    Read up to `sz` characters from `f` into `buf`. Returns the number of
//    characters read on success; normally this is `sz`. Returns a short
//    count, which might be zero, if the file ended before `sz` characters
//    could be read. Returns -1 if an error occurred before any characters
//    were read.

ssize_t io61_read(io61_file* f, unsigned char* buf, size_t sz) {
    size_t pos = 0;
    while (pos < sz) {
        // Refill the cache if pos_tag reaches the end or is out of bounds
        if (f->pos_tag >= f->end_tag || f->pos_tag < f->tag) {
            int n = io61_fill(f);
            if (n == 0) {
                break;
            } else if (n == -1) {
                return -1;
            }
        }
        // Read as many characters as possible until we reach the end of the cache or hit sz
        size_t read = std::min(sz - pos, (size_t) (f->end_tag - f->pos_tag));
        memcpy(&buf[pos], &f->cbuf[f->pos_tag - f->tag], read);
        f->pos_tag += read;
        pos += read;
    }
    return pos;
}


// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file* f) {
    // Refill the cache if pos_tag reaches the limit or is out of bounds
    if (f->pos_tag >= f->end_tag || f->pos_tag < f->tag) {
        int n = io61_fill(f);
        if (n <= 0) {
            // If there was an error or we reached EOF, return -1
            return -1;
        }
    }
    // Then read the character and update the position to the next character
    unsigned char ch = f->cbuf[f->pos_tag - f->tag];
    f->pos_tag++;
    return ch;
}


// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file* f, const unsigned char* buf, size_t sz) {
    // If pos_tag is out of bounds, reset the cache
    if (f->pos_tag < f->tag || f->pos_tag > f->tag + f->bufsize) {
        f->tag = f->end_tag = f->pos_tag;
    }

    size_t pos = 0;
    while (pos < sz) {
        // Flush the cache if it is full
        if (f->end_tag == f->tag + f->bufsize) {
            int n = io61_flush(f);
            if (n < 0) {
                return -1;
            }
        }
        // Write as many characters as possible until we reach the end of the cache or hit sz
        size_t write = std::min(sz - pos, (size_t) (f->bufsize - (f->end_tag - f->tag)));
        memcpy(&f->cbuf[f->pos_tag - f->tag], &buf[pos], write);
        f->pos_tag += write;
        f->end_tag += write;
        pos += write;
    }
    return pos;
}


// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file* f, int ch) {
    // Flush the cache if it is full
    if (f->end_tag == f->tag + f->bufsize) {
        int n = io61_flush(f);
        if (n < 0) {
            return -1;
        }
    }

    // If pos_tag is within bounds, cache the character
    if (f->pos_tag >= f->tag && f->pos_tag <= f->tag + f->bufsize) {
        f->cbuf[f->end_tag - f->tag] = ch;
        f->pos_tag++;
        f->end_tag++;
        return 0;
    } else {
        return -1;
    }
}


// io61_flush(f)
//    Forces a write of all buffered data written to `f`.
//    If `f` was opened read-only, io61_flush(f) may either drop all
//    data buffered for reading, or do nothing.

int io61_flush(io61_file* f) {
    // Write the contents of the cache
    int n = write(f->fd, f->cbuf, f->end_tag - f->tag);

    // Reset the cache
    if (n == f->end_tag - f->tag) {
        f->tag = f->pos_tag = f->end_tag;
        return n;
    } else {
        return -1;
    }
}


// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file* f, off_t pos) {
    // If the file is write-only, flush the cache and lseek to the new position
    if (f->mode == O_WRONLY) {
        int n = io61_flush(f);
        if (n < 0) {
            return -1;
        }
        off_t r = lseek(f->fd, pos, SEEK_SET);
        if (r == pos) {
            return 0;
        } else {
            return -1;
        }
    }

    // If the new position is already in the cache, update the pos_tag
    if (pos >= f->tag && pos < f->end_tag) {
        f->pos_tag = pos;
        return 0;
    } else {
        // Otherwise, lseek to the nearest multiple of (bufsize) and fill the cache
        off_t aligned_pos = (pos / f->bufsize) * f->bufsize;
        off_t r = lseek(f->fd, aligned_pos, SEEK_SET);
        if (r == aligned_pos) {
            f->end_tag = aligned_pos;
            ssize_t n = io61_fill(f);
            if (n < 0) {
                return -1;
            }
            f->pos_tag = pos;
            return 0;
        } else {
            return -1;
        }
    }
}


// You shouldn't need to change these functions.

// io61_open_check(filename, mode)
//    Open the file corresponding to `filename` and return its io61_file.
//    If `!filename`, returns either the standard input or the
//    standard output, depending on `mode`. Exits with an error message if
//    `filename != nullptr` and the named file cannot be opened.

io61_file* io61_open_check(const char* filename, int mode) {
    int fd;
    if (filename) {
        fd = open(filename, mode, 0666);
    } else if ((mode & O_ACCMODE) == O_RDONLY) {
        fd = STDIN_FILENO;
    } else {
        fd = STDOUT_FILENO;
    }
    if (fd < 0) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(1);
    }
    return io61_fdopen(fd, mode & O_ACCMODE);
}


// io61_filesize(f)
//    Return the size of `f` in bytes. Returns -1 if `f` does not have a
//    well-defined size (for instance, if it is a pipe).

off_t io61_filesize(io61_file* f) {
    struct stat s;
    int r = fstat(f->fd, &s);
    if (r >= 0 && S_ISREG(s.st_mode)) {
        return s.st_size;
    } else {
        return -1;
    }
}
