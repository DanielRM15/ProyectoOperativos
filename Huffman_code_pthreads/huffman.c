#include "utf8decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>

//function to get optimal thread count
int getOptimalThreadCount() {
    int cores = sysconf(_SC_NPROCESSORS_ONLN);
    
    printf("Detected %d cores, using %d threads\n", cores, cores);
    return cores;
}

typedef struct {
    char filepath[1024];
    int *localFreq;
} FreqTask;

typedef struct {
    char filepath[1024];
    char filename[256];
    char **codes;
    unsigned char *compressedData;
    size_t compressedSize;
    int characterCount;
} CompressTask;

typedef struct {
    char filename[256];
    char outputPath[1024];
    unsigned char *compressedData;
    size_t compressedSize;
    int characterCount;
    int uniqueChars;
    struct MinHeapNode *root;
} DecompressTask;

pthread_mutex_t freqMutex = PTHREAD_MUTEX_INITIALIZER;
int *globalFreq;



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

void *freqWorker(void *arg) {
    FreqTask *task = (FreqTask *)arg;
    task->localFreq = calloc(UNICODE_MAX, sizeof(int));
    if (!task->localFreq) pthread_exit(NULL);

    FILE *f = fopen(task->filepath, "rb");
    if (!f) pthread_exit(NULL);

    int cp;
    while ((cp = getUTF8Char(f)) != EOF) {
        if (cp >= 0 && cp < UNICODE_MAX) {
            task->localFreq[cp]++;
        }
    }
    fclose(f);

    //merge into global
    pthread_mutex_lock(&freqMutex);
    for (int i = 0; i < UNICODE_MAX; i++) {
        if (task->localFreq[i] > 0)
            globalFreq[i] += task->localFreq[i];
    }
    pthread_mutex_unlock(&freqMutex);

    free(task->localFreq);
    pthread_exit(NULL);
}

void *compressWorker(void *arg) {
    CompressTask *task = (CompressTask *)arg;
    FILE *in = fopen(task->filepath, "rb");
    if (!in) pthread_exit(NULL);

    int cp;
    task->characterCount = 0;
    while ((cp = getUTF8Char(in)) != EOF) {
        task->characterCount++;
    }
    rewind(in);

    size_t bufCap = task->characterCount * 8;
    task->compressedData = malloc(bufCap);
    size_t pos = 0, bitCount = 0;
    unsigned char buffer = 0;

    while ((cp = getUTF8Char(in)) != EOF) {
        char *code = task->codes[cp];
        if (!code) continue;
        for (int i = 0; code[i]; i++) {
            buffer = (buffer << 1) | (code[i] - '0');
            bitCount++;
            if (bitCount == 8) {
                task->compressedData[pos++] = buffer;
                buffer = 0;
                bitCount = 0;
            }
        }
    }
    if (bitCount > 0) {
        buffer <<= (8 - bitCount);
        task->compressedData[pos++] = buffer;
    }
    task->compressedSize = pos;

    fclose(in);
    pthread_exit(NULL);
}

void *decompressWorker(void *arg) {
    DecompressTask *task = (DecompressTask *)arg;
    
    FILE *out = fopen(task->outputPath, "wb");
    if (!out) {
        printf("Error creating file: %s\n", task->outputPath);
        pthread_exit(NULL);
    }

    struct MinHeapNode* current = task->root;
    int decodedChars = 0;
    size_t byteIndex = 0;

    if (task->uniqueChars == 1) {
        //one character
        while (decodedChars < task->characterCount) {
            writeUTF8Char(out, task->root->data);
            decodedChars++;
        }
    } else {
        //normal Huffman decoding
        while (decodedChars < task->characterCount && byteIndex < task->compressedSize) {
            unsigned char buffer = task->compressedData[byteIndex++];
            
            for (int bit = 7; bit >= 0 && decodedChars < task->characterCount; bit--) {
                int bitValue = (buffer >> bit) & 1;
                
                if (bitValue == 0) {
                    current = current->left;
                } else {
                    current = current->right;
                }
                
                if (isLeaf(current)) {
                    writeUTF8Char(out, current->data);
                    current = task->root;
                    decodedChars++;
                }
            }
        }
    }

    fclose(out);
    printf("Decompressed: %s\n", task->filename);
    pthread_exit(NULL);
}

void HuffmanCompressDirectory(const char *dirPath) {
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    DIR *dir = opendir(dirPath);
    if (!dir) {
        perror("opendir");
        return;
    }

    //count .txt files
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

    //parallel frequency counting with thread pool
    globalFreq = calloc(UNICODE_MAX, sizeof(int));
    rewinddir(dir);

    //use thread pool for frequency counting
    int maxFreqThreads = getOptimalThreadCount();
    int numFreqThreads = fileCount < maxFreqThreads ? fileCount : maxFreqThreads;
    
    printf("Using %d threads for frequency counting\n", numFreqThreads);

    pthread_t *freqThreads = malloc(numFreqThreads * sizeof(pthread_t));
    FreqTask *freqTasks = malloc(fileCount * sizeof(FreqTask));

    int fIndex = 0;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcmp(entry->d_name + len - 4, ".txt") == 0) {
            snprintf(freqTasks[fIndex].filepath, sizeof(freqTasks[fIndex].filepath),
                     "%s/%s", dirPath, entry->d_name);
            fIndex++;
        }
    }
    
    //process frequency counting
    int freqTaskIndex = 0;
    while (freqTaskIndex < fileCount) {
        int threadsThisBatch = (fileCount - freqTaskIndex < numFreqThreads) ? (fileCount - freqTaskIndex) : numFreqThreads;
        
        //start frequency threads
        for (int i = 0; i < threadsThisBatch; i++) {
            pthread_create(&freqThreads[i], NULL, freqWorker, &freqTasks[freqTaskIndex + i]);
        }
        
        //wait for batch to complete
        for (int i = 0; i < threadsThisBatch; i++) {
            pthread_join(freqThreads[i], NULL);
        }
        
        freqTaskIndex += threadsThisBatch;
    }
    
    free(freqThreads);
    free(freqTasks);

    //build Huffman tree and codes (single thread)
    int uniqueChars = 0;
    for (int i = 0; i < UNICODE_MAX; i++)
        if (globalFreq[i] > 0) uniqueChars++;

    int *data = malloc(uniqueChars * sizeof(int));
    int *freqList = malloc(uniqueChars * sizeof(int));
    int idx = 0;
    for (int i = 0; i < UNICODE_MAX; i++) {
        if (globalFreq[i] > 0) {
            data[idx] = i;
            freqList[idx] = globalFreq[i];
            idx++;
        }
    }
    struct MinHeapNode* root = HuffmanTree(data, freqList, uniqueChars);
    char **codes = calloc(UNICODE_MAX, sizeof(char*));
    char *tmp = malloc(UNICODE_MAX);
    if (uniqueChars == 1) codes[data[0]] = strdup("0");
    else buildCodes(root, tmp, 0, codes);

    //parallel compression per file
    FILE *out = fopen("archive.huff", "wb");
    if (!out) { 
      perror("fopen"); 
      goto cleanup; 
    }

    //header
    fwrite(&uniqueChars, sizeof(int), 1, out);
    for (int i = 0; i < uniqueChars; i++) {
        fwrite(&data[i], sizeof(int), 1, out);
        fwrite(&freqList[i], sizeof(int), 1, out);
    }
    fwrite(&fileCount, sizeof(int), 1, out);

    rewinddir(dir);
    //use thread pool for compression
    int maxCompThreads = getOptimalThreadCount();
    int numCompThreads = fileCount < maxCompThreads ? fileCount : maxCompThreads;
    
    printf("Using %d threads for compression\n", numCompThreads);
    
    pthread_t *compThreads = malloc(numCompThreads * sizeof(pthread_t));
    CompressTask *compTasks = malloc(fileCount * sizeof(CompressTask));

    //populate all tasks
    fIndex = 0;
    rewinddir(dir);
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcmp(entry->d_name + len - 4, ".txt") == 0) {
            snprintf(compTasks[fIndex].filepath, sizeof(compTasks[fIndex].filepath),
                     "%s/%s", dirPath, entry->d_name);
            strcpy(compTasks[fIndex].filename, entry->d_name);
            compTasks[fIndex].codes = codes;
            fIndex++;
        }
    }
    
    //process with threads
    int compTaskIndex = 0;
    while (compTaskIndex < fileCount) {
        int threadsThisBatch = (fileCount - compTaskIndex < numCompThreads) ? (fileCount - compTaskIndex) : numCompThreads;
        
        //start compression threads
        for (int i = 0; i < threadsThisBatch; i++) {
            pthread_create(&compThreads[i], NULL, compressWorker, &compTasks[compTaskIndex + i]);
        }
        
        //wait for threads to complete and write results
        for (int i = 0; i < threadsThisBatch; i++) {
            pthread_join(compThreads[i], NULL);
            
            //sequential archive writing
            int taskIdx = compTaskIndex + i;
            int nameLen = strlen(compTasks[taskIdx].filename);
            fwrite(&nameLen, sizeof(int), 1, out);
            fwrite(compTasks[taskIdx].filename, 1, nameLen, out);
            fwrite(&compTasks[taskIdx].characterCount, sizeof(int), 1, out);
            fwrite(&compTasks[taskIdx].compressedSize, sizeof(size_t), 1, out);
            fwrite(compTasks[taskIdx].compressedData, 1, compTasks[taskIdx].compressedSize, out);

            free(compTasks[taskIdx].compressedData);
            printf("Compressed: %s\n", compTasks[taskIdx].filename);
        }
        
        compTaskIndex += threadsThisBatch;
    }
    fclose(out);
    free(compThreads);
    free(compTasks);

    //timings and cleanup
    gettimeofday(&end_time, NULL);
    double compression_time = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
        (end_time.tv_usec - start_time.tv_usec) / 1000.0;
    printf("Directory compressed successfully to archive.huff\n");
    printf("Compression time: %.3f ms\n", compression_time);

cleanup:
    closedir(dir);
    for (int i = 0; i < UNICODE_MAX; i++) if (codes[i]) free(codes[i]);
    free(codes); free(data); free(freqList); free(tmp); free(globalFreq);
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
  
  //read compressed data sequentially
  DecompressTask *decompTasks = malloc(fileCount * sizeof(DecompressTask));
  
  for (int f = 0; f < fileCount; f++) {
    int nameLen;
    fread(&nameLen, sizeof(int), 1, in);
    
    char filename[256];
    fread(filename, 1, nameLen, in);
    filename[nameLen] = '\0';
    strcpy(decompTasks[f].filename, filename);
    
    //read character count for this file
    fread(&decompTasks[f].characterCount, sizeof(int), 1, in);
    
    snprintf(decompTasks[f].outputPath, sizeof(decompTasks[f].outputPath), 
             "%s/%s", outputDir, filename);
    
    //calculate compressed size for this file
    if (uniqueChars == 1) {
      decompTasks[f].compressedSize = (decompTasks[f].characterCount + 7) / 8;
    } else {
      //for multi character we need to read until we decode all characters
      decompTasks[f].compressedSize = decompTasks[f].characterCount * 2; //conservative estimate
    }
    
    if (uniqueChars == 1) {
      decompTasks[f].compressedData = malloc(decompTasks[f].compressedSize);
      fread(decompTasks[f].compressedData, 1, decompTasks[f].compressedSize, in);
    } else {

      fread(&decompTasks[f].compressedSize, sizeof(size_t), 1, in);

      decompTasks[f].compressedData = malloc(decompTasks[f].compressedSize);
      fread(decompTasks[f].compressedData, 1, decompTasks[f].compressedSize, in);
    }
    
    decompTasks[f].uniqueChars = uniqueChars;
    decompTasks[f].root = root;
  }
  
  fclose(in);

  //use thread pool for decompression
  int maxDecompThreads = getOptimalThreadCount();

  printf("Using %d threads for decompression\n", maxDecompThreads);

  pthread_t *decompThreads = malloc(fileCount * sizeof(pthread_t));
  
  //start all decompression threads simultaneously
  for (int i = 0; i < fileCount; i++) {
    pthread_create(&decompThreads[i], NULL, decompressWorker, &decompTasks[i]);
  }
  
  //wait for all threads to complete
  for (int i = 0; i < fileCount; i++) {
    pthread_join(decompThreads[i], NULL);
    free(decompTasks[i].compressedData);
  }
  
  free(decompThreads);
  free(decompTasks);
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
