#include "utf8decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>

//function to get optimal process count
int getOptimalProcessCount() {
    int cores = sysconf(_SC_NPROCESSORS_ONLN);
    printf("Detected %d cores, using %d processes\n", cores, cores);
    return cores;
}

//shared memory structure for frequencies
typedef struct {
    int freq[UNICODE_MAX];
    int completed_files;
} SharedFreqData;

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

//process for frequency counting using mmap
void processFrequencyCount(SharedFreqData *sharedData, char filepaths[][1024], int start, int end) {
    for (int i = start; i < end; i++) {
        FILE *f = fopen(filepaths[i], "rb");
        if (!f) continue;

        int cp;
        while ((cp = getUTF8Char(f)) != EOF) {
            if (cp >= 0 && cp < UNICODE_MAX) {
                sharedData->freq[cp]++;
            }
        }
        fclose(f);
        sharedData->completed_files++;
    }
}

//process for compression using mmap
void processCompression(char filepaths[][1024], char filenames[][256], char **codes, int start, int end, FILE *out) {
    for (int i = start; i < end; i++) {
        FILE *in = fopen(filepaths[i], "rb");
        if (!in) continue;

        //count characters
        int cp;
        int characterCount = 0;
        while ((cp = getUTF8Char(in)) != EOF) {
            characterCount++;
        }
        rewind(in);

        size_t bufCap = characterCount * 8;
        unsigned char *compressedData = malloc(bufCap);
        size_t pos = 0, bitCount = 0;
        unsigned char buffer = 0;

        //compress the file
        while ((cp = getUTF8Char(in)) != EOF) {
            char *code = codes[cp];
            if (!code) continue;
            for (int j = 0; code[j]; j++) {
                buffer = (buffer << 1) | (code[j] - '0');
                bitCount++;
                if (bitCount == 8) {
                    compressedData[pos++] = buffer;
                    buffer = 0;
                    bitCount = 0;
                }
            }
        }
        if (bitCount > 0) {
            buffer <<= (8 - bitCount);
            compressedData[pos++] = buffer;
        }

        //write to archive
        int nameLen = strlen(filenames[i]);
        fwrite(&nameLen, sizeof(int), 1, out);
        fwrite(filenames[i], 1, nameLen, out);
        fwrite(&characterCount, sizeof(int), 1, out);
        fwrite(&pos, sizeof(size_t), 1, out);
        fwrite(compressedData, 1, pos, out);

        free(compressedData);
        fclose(in);
        printf("Compressed: %s\n", filenames[i]);
    }
}

//process for decompression
void processDecompression(char (*filenames)[256], char (*outputPaths)[1024], 
                         unsigned char *compressedDataBlock, size_t *compressedSizes, 
                         int *characterCounts, struct MinHeapNode* root, int uniqueChars,
                         int start, int end) {
    for (int i = start; i < end; i++) {
        FILE *out = fopen(outputPaths[i], "wb");
        if (!out) {
            printf("Error creating file: %s\n", outputPaths[i]);
            continue;
        }

        struct MinHeapNode* current = root;
        int decodedChars = 0;
        size_t byteIndex = 0;
        unsigned char *fileData = compressedDataBlock + i * 1024 * 1024; // Get this file's data

        if (uniqueChars == 1) {
            while (decodedChars < characterCounts[i]) {
                writeUTF8Char(out, root->data);
                decodedChars++;
            }
        } else {
            //normal Huffman decoding
            while (decodedChars < characterCounts[i] && byteIndex < compressedSizes[i]) {
                unsigned char buffer = fileData[byteIndex++];
                
                for (int bit = 7; bit >= 0 && decodedChars < characterCounts[i]; bit--) {
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
        printf("Decompressed: %s\n", filenames[i]);
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

    //count .txt files and store paths
    char filepaths[100][1024];
    char filenames[100][256];
    int fileCount = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcmp(entry->d_name + len - 4, ".txt") == 0) {
            snprintf(filepaths[fileCount], sizeof(filepaths[fileCount]), "%s/%s", dirPath, entry->d_name);
            strcpy(filenames[fileCount], entry->d_name);
            fileCount++;
            if (fileCount >= 100) break;
        }
    }
    closedir(dir);
    
    if (fileCount == 0) {
        fprintf(stderr, "No .txt files found in directory\n");
        return;
    }

    printf("Found %d .txt files to compress\n", fileCount);

    //create shared memory for frequency counting
    SharedFreqData *sharedFreqData = mmap(NULL, sizeof(SharedFreqData), 
                                         PROT_READ | PROT_WRITE, 
                                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sharedFreqData == MAP_FAILED) {
        perror("mmap failed");
        return;
    }

    //initialize shared memory
    memset(sharedFreqData, 0, sizeof(SharedFreqData));

    //parallel frequency counting with fork
    int numProcesses = getOptimalProcessCount();
    if (numProcesses > fileCount) numProcesses = fileCount;
    
    printf("Using %d processes for frequency counting\n", numProcesses);

    pid_t pids[numProcesses];
    int filesPerProcess = fileCount / numProcesses;
    int remainder = fileCount % numProcesses;

    for (int i = 0; i < numProcesses; i++) {
        int start = i * filesPerProcess;
        int end = start + filesPerProcess;
        if (i == numProcesses - 1) end += remainder;

        pids[i] = fork();
        if (pids[i] == 0) {
            //child process
            processFrequencyCount(sharedFreqData, filepaths, start, end);
            exit(0);
        } else if (pids[i] < 0) {
            perror("fork failed");
            exit(1);
        }
    }

    //wait for all frequency counting processes
    for (int i = 0; i < numProcesses; i++) {
        waitpid(pids[i], NULL, 0);
    }

    //build Huffman tree from global frequencies
    int uniqueChars = 0;
    for (int i = 0; i < UNICODE_MAX; i++) {
        if (sharedFreqData->freq[i] > 0) uniqueChars++;
    }

    if (uniqueChars == 0) {
        fprintf(stderr, "No valid characters found\n");
        munmap(sharedFreqData, sizeof(SharedFreqData));
        return;
    }

    int *data = malloc(uniqueChars * sizeof(int));
    int *freqList = malloc(uniqueChars * sizeof(int));
    
    int idx = 0;
    for (int i = 0; i < UNICODE_MAX; i++) {
        if (sharedFreqData->freq[i] > 0) {
            data[idx] = i;
            freqList[idx] = sharedFreqData->freq[i];
            idx++;
        }
    }

    struct MinHeapNode* root = HuffmanTree(data, freqList, uniqueChars);
    
    //build huffman codes
    char **codes = calloc(UNICODE_MAX, sizeof(char*));
    char *tmp = malloc(UNICODE_MAX);
    
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

    //sequential compression (to maintain file order in archive)
    for (int i = 0; i < fileCount; i++) {
        processCompression(filepaths, filenames, codes, i, i+1, out);
    }

    fclose(out);

    gettimeofday(&end_time, NULL);
    double compression_time = (end_time.tv_sec - start_time.tv_sec) +
        (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    printf("Directory compressed successfully to archive.huff\n");
    printf("Compression time: %.3f seconds\n", compression_time);

cleanup:
    munmap(sharedFreqData, sizeof(SharedFreqData));
    for (int i = 0; i < UNICODE_MAX; i++) {
        if (codes[i]) free(codes[i]);
    }
    free(codes);
    free(data);
    free(freqList);
    free(tmp);
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
    
    char (*filenames)[256] = malloc(fileCount * 256);
    char (*outputPaths)[1024] = malloc(fileCount * 1024);
    size_t *compressedSizes = malloc(fileCount * sizeof(size_t));
    int *characterCounts = malloc(fileCount * sizeof(int));
    
    size_t totalDataSize = fileCount * 1024 * 1024; // 1MB per file
    unsigned char *compressedDataBlock = mmap(NULL, totalDataSize, 
                                            PROT_READ | PROT_WRITE, 
                                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (compressedDataBlock == MAP_FAILED) {
        perror("mmap failed for compressed data");
        free(filenames);
        free(outputPaths);
        free(compressedSizes);
        free(characterCounts);
        free(data);
        free(freqList);
        fclose(in);
        return;
    }
    
    for (int f = 0; f < fileCount; f++) {
        int nameLen;
        fread(&nameLen, sizeof(int), 1, in);
        
        fread(filenames[f], 1, nameLen, in);
        filenames[f][nameLen] = '\0';
        
        fread(&characterCounts[f], sizeof(int), 1, in);
        
        snprintf(outputPaths[f], sizeof(outputPaths[f]), "%s/%s", outputDir, filenames[f]);
        
        if (uniqueChars == 1) {
            compressedSizes[f] = (characterCounts[f] + 7) / 8;
            fread(compressedDataBlock + f * 1024 * 1024, 1, compressedSizes[f], in);
        } else {
            fread(&compressedSizes[f], sizeof(size_t), 1, in);
            fread(compressedDataBlock + f * 1024 * 1024, 1, compressedSizes[f], in);
        }
    }
    
    fclose(in);
    
    //parallel decompression with fork
    int numProcesses = getOptimalProcessCount();
    if (numProcesses > fileCount) numProcesses = fileCount;
    
    printf("Using %d processes for decompression\n", numProcesses);

    pid_t pids[numProcesses];
    int filesPerProcess = fileCount / numProcesses;
    int remainder = fileCount % numProcesses;

    for (int i = 0; i < numProcesses; i++) {
        int start = i * filesPerProcess;
        int end = start + filesPerProcess;
        if (i == numProcesses - 1) end += remainder;

        pids[i] = fork();
        if (pids[i] == 0) {
            //child process
            processDecompression(filenames, outputPaths, compressedDataBlock, compressedSizes, 
                               characterCounts, root, uniqueChars, start, end);
            exit(0);
        } else if (pids[i] < 0) {
            perror("fork failed");
            exit(1);
        }
    }

    //wait for all decompression processes
    for (int i = 0; i < numProcesses; i++) {
        waitpid(pids[i], NULL, 0);
    }
    
    //cleanup allocated memory
    free(filenames);
    free(outputPaths);
    free(compressedSizes);
    free(characterCounts);
    munmap(compressedDataBlock, totalDataSize);
    free(data);
    free(freqList);
    
    gettimeofday(&end_time, NULL);
    double decompression_time = (end_time.tv_sec - start_time.tv_sec) + 
        (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    printf("Directory decompression completed!\n");
    printf("Decompression time: %.3f seconds\n", decompression_time);
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