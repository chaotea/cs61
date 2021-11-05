#include "io61.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <climits>
#include <cerrno>

#define FORWARD 0
#define REVERSE 1

// io61.cc
//    YOUR CODE HERE!


// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

struct io61_file {
    int fd;
    static constexpr off_t bufsize = 4096;
    unsigned char cbuf[bufsize];
    off_t tag;
    off_t end_tag;
    off_t pos_tag;
    int dir;  // read direction
};


// io61_fdopen(fd, mode)
//    Return a new io61_file for file descriptor `fd`. `mode` is
//    either O_RDONLY for a read-only file or O_WRONLY for a
//    write-only file. You need not support read/write files.

io61_file* io61_fdopen(int fd, int mode) {
    assert(fd >= 0);
    io61_file* f = new io61_file;
    f->fd = fd;
    (void) mode;
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

int io61_fill(io61_file* f, int dir) {
    // Update the file descriptor offset and reset the cache
    lseek(f->fd, f->pos_tag, SEEK_SET);
    f->tag = f->end_tag = f->pos_tag;

    ssize_t n = -1;
    if (dir == REVERSE && f->pos_tag >= f->bufsize) {
        // If reverse-sequential access, fill in reverse
        // i.e. cache the previous (bufsize) characters
        lseek(f->fd, -f->bufsize + 1, SEEK_CUR);
        n = read(f->fd, f->cbuf, f->bufsize);
        if (n >= 0) {
            f->tag = f->end_tag - n + 1;
        } else {
            return -1;
        }
    } else {
        // Otherwise, cache the next (bufsize) characters
        n = read(f->fd, f->cbuf, f->bufsize);
        if (n >= 0) {
            f->end_tag = f->tag + n;
        } else {
            return -1;
        }
    }
    return n;
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
            ssize_t n = io61_fill(f, FORWARD);
            if (f->pos_tag == f->end_tag) {
                break;
            } else if (n == -1) {
                return -1;
            }
        }

        size_t read_sz = 0;
        if (sz - pos <= (size_t) f->end_tag - f->pos_tag) {
            // If we can read all the remaining characters at once, then do it
            read_sz = sz - pos;
            memcpy(&buf[pos], &f->cbuf[f->pos_tag - f->tag], read_sz);
        } else {
            // Otherwise, read as much as possible until the cache is full
            read_sz = f->end_tag - f->pos_tag;
            memcpy(&buf[pos], &f->cbuf[f->pos_tag - f->tag], read_sz);
        }

        // Update the position
        f->pos_tag += read_sz;
        pos += read_sz;
    }
    return pos;
}


// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file* f) {
    // Refill the cache if pos_tag reaches the limit or is out of bounds
    // if reading forward, the limit is the end;
    // if reading in reverse, the limit is one before the start.
    if (
        (f->pos_tag == f->end_tag && f->dir == FORWARD) ||
        (f->pos_tag == f->tag - 1 && f->dir == REVERSE) ||
        (f->pos_tag < f->tag || f->pos_tag > f->end_tag)
        )
    {
        ssize_t n = io61_fill(f, f->dir);
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
    if (f->pos_tag < f-> tag || f->pos_tag > f->end_tag) {
        f->tag = f->end_tag = f->pos_tag;
    }

    size_t pos = 0;
    while (pos < sz) {
        size_t write_sz = 0;
        if (sz - pos <= (size_t) f->bufsize - (f->pos_tag - f->tag)) {
            // If we can write all the remaining characters at once, then do it
            write_sz = sz - pos;
            memcpy(&f->cbuf[f->pos_tag - f->tag], &buf[pos], write_sz);
        } else {
            // Otherwise, write as much as possible until the cache is full
            write_sz = f->bufsize - (f->pos_tag - f->tag);
            memcpy(&f->cbuf[f->pos_tag - f->tag], &buf[pos], write_sz);
        }

        // Update the position
        f->pos_tag += write_sz;
        f->end_tag += write_sz;
        pos += write_sz;

        // If the cache is full, flush it
        if (f->end_tag == f->tag + f->bufsize) {
            io61_flush(f);
        }
    }
    return pos;
}


// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file* f, int ch) {
    // If the cache is full, flush it
    if (f->end_tag == f->tag + f->bufsize) {
        io61_flush(f);
    }
    if (f->pos_tag >= f->tag && f->pos_tag <= f->tag + f->bufsize) {
        // If pos_tag is within bounds, cache the character
        f->cbuf[f->end_tag - f->tag] = ch;
        f->pos_tag++;
        f->end_tag++;
        return 0;
    } else {
        // Otherwise, return an error
        return -1;
    }
}


// io61_flush(f)
//    Forces a write of all buffered data written to `f`.
//    If `f` was opened read-only, io61_flush(f) may either drop all
//    data buffered for reading, or do nothing.

int io61_flush(io61_file* f) {
    // Write the contents of the cache
    ssize_t n = write(f->fd, f->cbuf, f->pos_tag - f->tag);
    if (n == f->pos_tag - f-> tag) {
        // If the write is successful, reset the cache
        f->tag = f->pos_tag;
        return 0;
    } else {
        // Otherwise, return an error
        return -1;
    }
}


// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file* f, off_t pos) {
    // Update the file descriptor offset
    off_t r = lseek(f->fd, pos, SEEK_SET);
    if (r == pos) {
        // If successful, update pos_tag and check if
        // the file is being read reverse-sequentially
        f->pos_tag = r;
        if (f->pos_tag > f->end_tag) {
            f->dir = REVERSE;
        }
        return 0;
    } else {
        // Otherwise, return failure
        return -1;
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
