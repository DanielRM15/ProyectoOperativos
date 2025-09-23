#include "utf8decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>

//First, the tree structure
//Tree node
struct MinHeapNode {
  int data;
  unsigned freq;
  struct MinHeapNode* left,* right;
};

//Binary tree min heap (priority queue)
struct MinHeap {
  unsigned size;
  unsigned capacity;
  struct MinHeapNode** array;
};

//Create a new node using data and freq (this does not insert it into the tree)
struct MinHeapNode* newNode(int data, unsigned freq) {
  
  struct MinHeapNode* temp = (struct MinHeapNode*)malloc(sizeof(struct MinHeapNode));
  
  temp->left = temp->right = NULL;
  temp->data = data;
  temp->freq = freq;
  
  return temp;
}

//Create min heap using capacity
struct MinHeap* createMinHeap(unsigned capacity) {
  
  struct MinHeap* minHeap = (struct MinHeap*)malloc(sizeof(struct MinHeap));
  
  minHeap->size = 0;
  
  minHeap->capacity = capacity;
  
  minHeap->array = (struct MinHeapNode**)malloc(minHeap->capacity * sizeof(struct MinHeapNode*));
  return minHeap;
}

//swap two nodes
void swapNodes(struct MinHeapNode** a, struct MinHeapNode** b) {
  
  struct MinHeapNode* t = *a;
  *a = *b;
  *b = t;

}

//
void heapify(struct MinHeap* minHeap, int ind) {
  
  int smallest = ind;
  int leftNode = 2 * ind + 1;
  int rightNode = 2 * ind + 2;
  
  if(leftNode < minHeap->size && minHeap->array[leftNode]->freq < minHeap->array[smallest]->freq)
    smallest = leftNode;
  if(rightNode < minHeap->size && minHeap->array[rightNode]->freq < minHeap->array[smallest]->freq)
    smallest = rightNode;
  if(smallest != ind) {
    swapNodes(&minHeap->array[smallest],&minHeap->array[ind]);
    heapify(minHeap, smallest);
  }
}

//
int isSizeOne(struct MinHeap* minHeap) {

  return (minHeap->size == 1);

}

struct MinHeapNode* getMin(struct MinHeap* minHeap) {
  
  struct MinHeapNode* temp = minHeap->array[0];
  minHeap->array[0] = minHeap->array[minHeap->size - 1];
  
  --minHeap->size;
  heapify(minHeap, 0);
  
  return temp;
}

void insertNode(struct MinHeap* minHeap, struct MinHeapNode* minHeapNode) {
  
  ++minHeap->size;
  int i = minHeap->size - 1;
  
  while(i && minHeapNode->freq < minHeap->array[(i-1)/2]->freq) {
    minHeap->array[i] = minHeap->array[(i-1)/2];
    i = (i-1)/2;
  }
  
  minHeap->array[i] = minHeapNode;
}

void buildHeap(struct MinHeap* minHeap) {
  int n = minHeap->size-1;
  int i;
  
  for(i = (n-1)/2; i >= 0; --i)
    heapify(minHeap, i);
}

int isLeaf(struct MinHeapNode* root) {

  return !(root->left) && !(root->right);

}

struct MinHeap* createHeap(int data[], int freq[], int size) {

  struct MinHeap* minHeap = createMinHeap(size);
  
  for (int i = 0; i < size; i++)
    minHeap->array[i] = newNode(data[i],freq[i]);
  
  minHeap->size = size;
  buildHeap(minHeap);
  
  return minHeap;
}

struct MinHeapNode* HuffmanTree(int data[], int freq[], int size) {
  
  struct MinHeapNode *left, *right, *top;
  
  struct MinHeap* minHeap = createHeap(data, freq, size);
  
  //if only one character, create a simple tree
  if(size == 1) {
    return minHeap->array[0];
  }
  
  while(!isSizeOne(minHeap)) {
  
    left = getMin(minHeap);
    right = getMin(minHeap);
    
    top = newNode('$', left->freq + right->freq);
    
    top->left = left;
    top->right = right;
    
    insertNode(minHeap, top);
  }
  
  return getMin(minHeap);
}

void buildCodes(struct MinHeapNode* root, char *arr, int top, char *codes[]) {
  
  if(root->left) {
    arr[top] = '0';
    buildCodes(root->left, arr, top + 1, codes);
  }
  
  if(root->right) {
    arr[top] = '1';
    buildCodes(root->right, arr, top + 1, codes);
  }
  
  if(isLeaf(root)) {
    arr[top] = '\0';
    codes[root->data] = strdup(arr);
  }
}

void HuffmanCompressDirectory(const char *dirPath) {
  struct timeval start_time, end_time;
  gettimeofday(&start_time, NULL);
  
  DIR *dir = opendir(dirPath);
  if (!dir) {
    perror("opendir");
    return;
  }
  
  //count the number of .txt files
  int fileCount = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    size_t len = strlen(entry->d_name);
    if (len > 4 && strcmp(entry->d_name + len - 4, ".txt") == 0) {
      fileCount++;
    }
  }
  
  if (fileCount == 0) {
    fprintf(stderr, "No .txt files found in directory\n");
    closedir(dir);
    return;
  }
  
  printf("Found %d .txt files to compress\n", fileCount);
  
  //build combined frequency table (it counts frequencies of all files)
  int *globalFreq = calloc(UNICODE_MAX, sizeof(int)); //malloc but initialized to 0
  if (!globalFreq) {
    perror("malloc");
    closedir(dir);
    return;
  }
  rewinddir(dir);
  
  while ((entry = readdir(dir)) != NULL) {
    size_t len = strlen(entry->d_name);
    if (len > 4 && strcmp(entry->d_name + len - 4, ".txt") == 0) {
      char fullPath[1024];
      snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);
      
      //count frequencies for this file and add to global
      FILE *f = fopen(fullPath, "rb");
      if (!f) continue;
      
      int cp;
      while ((cp = getUTF8Char(f)) != EOF) {
        if (cp >= 0 && cp < UNICODE_MAX) {
          globalFreq[cp]++;
        }
      }
      fclose(f);
    }
  }
  
  //build Huffman tree from global frequencies
  int uniqueChars = 0;
  for (int i = 0; i < UNICODE_MAX; i++) {
    if (globalFreq[i] > 0) uniqueChars++;
  }
  
  if (uniqueChars == 0) {
    fprintf(stderr, "No valid characters found\n");
    closedir(dir);
    return;
  }
  
  int *data = malloc(uniqueChars * sizeof(int));
  int *freqList = malloc(uniqueChars * sizeof(int));
  
  if (!data || !freqList) {
    perror("malloc");
    closedir(dir);
    return;
  }
  
  int idx = 0;
  for (int i = 0; i < UNICODE_MAX; i++) {
    if (globalFreq[i] > 0) {
      data[idx] = i;
      freqList[idx] = globalFreq[i];
      idx++;
    }
  }
  
  struct MinHeapNode* root = HuffmanTree(data, freqList, uniqueChars);
  
  //build huffman codes
  char **codes = calloc(UNICODE_MAX, sizeof(char*));
  char *tmp = malloc(UNICODE_MAX * sizeof(char));
  
  if (!codes || !tmp) {
    perror("malloc");
    goto cleanup;
  }
  
  if (uniqueChars == 1) {
    codes[data[0]] = strdup("0");
  } else {
    buildCodes(root, tmp, 0, codes);
  }
  
  //create archive file
  FILE *out = fopen("archive.huff", "wb");
  if (!out) {
    perror("fopen");
    goto cleanup;
  }
  
  //write header
  fwrite(&uniqueChars, sizeof(int), 1, out);
  for (int i = 0; i < uniqueChars; i++) {
    fwrite(&data[i], sizeof(int), 1, out);
    fwrite(&freqList[i], sizeof(int), 1, out);
  }
  fwrite(&fileCount, sizeof(int), 1, out);
  
  //compress each file
  rewinddir(dir);
  while ((entry = readdir(dir)) != NULL) {
    size_t len = strlen(entry->d_name);
    if (len > 4 && strcmp(entry->d_name + len - 4, ".txt") == 0) {
      char fullPath[1024];
      snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);
      
      //count characters in the file
      FILE *countFile = fopen(fullPath, "rb");
      if (!countFile) continue;
      
      int characterCount = 0;
      int cp;
      while ((cp = getUTF8Char(countFile)) != EOF) {
        characterCount++;
      }
      fclose(countFile);
      
      //write filename and character count
      int nameLen = strlen(entry->d_name);
      fwrite(&nameLen, sizeof(int), 1, out);
      fwrite(entry->d_name, 1, nameLen, out);
      fwrite(&characterCount, sizeof(int), 1, out);  //store character count
      
      //compress file content
      FILE *in = fopen(fullPath, "rb");
      if (!in) continue;
      
      unsigned char buffer = 0;
      int bitCount = 0;
      
      while ((cp = getUTF8Char(in)) != EOF) {
        char *code = codes[cp];
        if (!code) continue;
        
        for (int i = 0; code[i] != '\0'; i++) {
          buffer = (buffer << 1) | (code[i] - '0');
          bitCount++;
          
          if (bitCount == 8) {
            fwrite(&buffer, 1, 1, out);
            buffer = 0;
            bitCount = 0;
          }
        }
      }
      
      if (bitCount > 0) {
        buffer = buffer << (8 - bitCount);
        fwrite(&buffer, 1, 1, out);
      }
      
      fclose(in);
      
      printf("Compressed: %s\n", entry->d_name);
    }
  }
  
  fclose(out);
  
  gettimeofday(&end_time, NULL);
  double compression_time = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + (end_time.tv_usec - start_time.tv_usec) / 1000.0;
  
  printf("Directory compressed successfully to archive.huff\n");
  printf("Compression time: %.3f ms\n", compression_time);
  
cleanup:
  closedir(dir);
  for (int i = 0; i < UNICODE_MAX; i++) {
    if (codes[i]) free(codes[i]);
  }
  free(codes);
  free(data);
  free(freqList);
  free(tmp);
  free(globalFreq);
}

void HuffmanDecompressDirectory(const char *archiveFile, const char *outputDir) {
  struct timeval start_time, end_time;
  gettimeofday(&start_time, NULL);
  
  FILE *in = fopen(archiveFile, "rb");
  if (!in) {
    perror("fopen");
    return;
  }
  
  //create output directory
  mkdir(outputDir, 0755);
  
  //read header
  int uniqueChars;
  fread(&uniqueChars, sizeof(int), 1, in);
  
  int *data = malloc(uniqueChars * sizeof(int));
  int *freqList = malloc(uniqueChars * sizeof(int));
  
  for (int i = 0; i < uniqueChars; i++) {
    fread(&data[i], sizeof(int), 1, in);
    fread(&freqList[i], sizeof(int), 1, in);
  }
  
  int fileCount;
  fread(&fileCount, sizeof(int), 1, in);
  
  struct MinHeapNode* root = HuffmanTree(data, freqList, uniqueChars);
  
  printf("Decompressing %d files to directory: %s\n", fileCount, outputDir);
  
  //decompress each file
  for (int f = 0; f < fileCount; f++) {
    int nameLen;
    fread(&nameLen, sizeof(int), 1, in);
    
    char *filename = malloc(nameLen + 1);
    fread(filename, 1, nameLen, in);
    filename[nameLen] = '\0';
    
    //read character count for this file
    int characterCount;
    fread(&characterCount, sizeof(int), 1, in);
    
    char outputPath[1024];
    snprintf(outputPath, sizeof(outputPath), "%s/%s", outputDir, filename);
    
    FILE *out = fopen(outputPath, "wb");
    if (!out) {
      free(filename);
      continue;
    }
    
    //decompress exactly characterCount characters
    struct MinHeapNode* current = root;
    unsigned char buffer;
    int decodedChars = 0;
    
    if (uniqueChars == 1) {
      //only one character type
      while (decodedChars < characterCount) {
        writeUTF8Char(out, root->data);
        decodedChars++;
      }
      //skip any remaining padding bits by reading until consumed all bits for this file (this is for the last byte)
      //we also need to calculate how many bytes were written during compression, so we can skip to the next file
      int bitsNeeded = characterCount;
      int bytesToSkip = (bitsNeeded + 7) / 8; //round up to nearest byte
      for (int i = 0; i < bytesToSkip; i++) {
        fread(&buffer, 1, 1, in);
      }
    } else {
      //normal Huffman decoding
      while (decodedChars < characterCount && fread(&buffer, 1, 1, in) == 1) {
        //normal bit by bit decoding
        for (int bit = 7; bit >= 0 && decodedChars < characterCount; bit--) {
          int bitValue = (buffer >> bit) & 1;
          
          if (bitValue == 0) {
            current = current->left;
          } else {
            current = current->right;
          }
          
          if (isLeaf(current)) {
            writeUTF8Char(out, current->data);
            current = root;
            decodedChars++;
          }
        }
      }
    }
    
    fclose(out);
    free(filename);
    printf("Decompressed: %s\n", outputPath);
  }
  
  fclose(in);
  free(data);
  free(freqList);
  
  gettimeofday(&end_time, NULL);
  double decompression_time = (end_time.tv_sec - start_time.tv_sec) * 1000.0 + (end_time.tv_usec - start_time.tv_usec) / 1000.0;

  printf("Directory decompression completed!\n");
  printf("Decompression time: %.3f ms\n", decompression_time);
}

int main(int argc, char *argv[]) {

  if (argc < 3) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, " Compress directory: %s -cd <directory>\n", argv[0]);
    fprintf(stderr, " Decompress directory: %s -dd <archive.huff> <output_directory>\n", argv[0]);
    return 1;
  }
  
  char *flag = argv[1];
  char *inputFile = argv[2];

  if (strcmp(flag, "-cd") == 0) {
    //compress directory
    HuffmanCompressDirectory(inputFile);
  } else if (strcmp(flag, "-dd") == 0) {
    //decompress directory
    if (argc < 4) {
      fprintf(stderr, "Usage: %s -dd <archive.huff> <output_directory>\n", argv[0]);
      return 1;
    }
    HuffmanDecompressDirectory(inputFile, argv[3]);
  } else {
    fprintf(stderr, "Unknown flag: %s\n", flag);
    fprintf(stderr, "Available options:\n");
    fprintf(stderr, " -cd: Compress directory\n");
    fprintf(stderr, " -dd: Decompress directory\n");
    return 1;
  }
  
  return 0;
}



