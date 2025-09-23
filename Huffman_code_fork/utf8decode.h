#ifndef UTF8DECODE_H
#define UTF8DECODE_H
#define UNICODE_MAX 0x110000
#include <stdio.h>

int getUTF8Char(FILE *f);
void countUTF8Frequencies(const char *filename, int freq[]);
void writeUTF8Char(FILE *f, int codepoint);

#endif
