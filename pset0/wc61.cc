#include <cstdio>
#include <cctype>

int main() {
    unsigned long lines = 0, words = 0, bytes = 0;
    int c = fgetc(stdin);
    bool current_is_word = false;
    bool next_is_word;
    while (c != EOF) {
        if (c == '\n') lines++;
        next_is_word = !isspace(c);
        if (!current_is_word && next_is_word) words++;
        current_is_word = next_is_word;
        bytes++;
        c = fgetc(stdin);
    }
    fprintf(stdout, "%8lu %8lu %8lu\n", lines, words, bytes);
}