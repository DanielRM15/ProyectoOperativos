#include "utf8decode.h"
#include <stdio.h>
#include <stdint.h>

int getUTF8Char(FILE *f) {

  int c = fgetc(f);
  if (c == EOF) return EOF;
  
  //Bit mask for 1 byte character
  if((c & 0x80) == 0) return c;
  
  //Bit mask for 2 byte character
  if((c & 0xE0) == 0xC0) {
    int c2 = fgetc(f);
    if(c2 == EOF) return -1;
    return ((c & 0x1F) << 6) | (c2 & 0x3F);
  }
  
  //Bit mask for 3 byte character
  if((c & 0xF0) == 0xE0) {
    int c2 = fgetc(f);
    int c3 = fgetc(f);
    if(c2 == EOF || c3 == EOF) return -1;
    return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);  
  }
  
  //Bit mask for 4 byte character
  if((c & 0xF8) == 0xF0) {
    int c2 = fgetc(f);
    int c3 = fgetc(f);
    int c4 = fgetc(f);
    if(c2 == EOF || c3 == EOF || c4 == EOF) return -1;
    return ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
  }
  
  return -1;
}


void countUTF8Frequencies(const char *filename, int freq[]) {
  FILE *f = fopen(filename, "rb");
  if (!f) {
    perror("file");
    return;  
  }
  
  for(int i = 0; i < 0x110000; i++) freq[i] = 0;
  
  int cp;
  while ((cp = getUTF8Char(f)) != EOF) {
    if(cp >= 0 && cp < 0x110000) freq[cp]++;
  }
  
  fclose(f);
}

void writeUTF8Char(FILE *f, int codepoint) {
  if (codepoint < 0 || codepoint >= 0x110000) {
    return; //Invalid codepoint
  }
  
  if (codepoint < 0x80) {
    //1 byte character (ASCII)
    fputc(codepoint, f);
  } else if (codepoint < 0x800) {
    //2 byte character
    fputc(0xC0 | (codepoint >> 6), f);
    fputc(0x80 | (codepoint & 0x3F), f);
  } else if (codepoint < 0x10000) {
    //3 byte character
    fputc(0xE0 | (codepoint >> 12), f);
    fputc(0x80 | ((codepoint >> 6) & 0x3F), f);
    fputc(0x80 | (codepoint & 0x3F), f);
  } else {
    //4 byte character
    fputc(0xF0 | (codepoint >> 18), f);
    fputc(0x80 | ((codepoint >> 12) & 0x3F), f);
    fputc(0x80 | ((codepoint >> 6) & 0x3F), f);
    fputc(0x80 | (codepoint & 0x3F), f);
  }
}
