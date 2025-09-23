#! /bin/bash

echo "Installing dependencies..."
su -c "apt-get install -y gcc"
su -c "apt-get install -y make"

echo "Building all Huffman implementations..."

echo "Building sequential implementation..."
cd "Huffman_code"
make clean
make
if [ $? -eq 0 ]; then
    echo "Sequential implementation built successfully"
else
    echo "Failed to build sequential implementation"
fi
cd ..

echo "Building fork implementation..."
cd "Huffman_code_fork"
make clean
make
if [ $? -eq 0 ]; then
    echo "Fork implementation built successfully"
else
    echo "Failed to build fork implementation"
fi
cd ..

echo "Building pthreads implementation..."
cd "Huffman_code_pthreads"
make clean
make
if [ $? -eq 0 ]; then
    echo "Pthreads implementation built successfully"
else
    echo "Failed to build pthreads implementation"
fi
cd ..

echo "Setup complete. All implementations have been built"
echo "You can now run the executables in their directories:"
echo "  - Sequential: ./Huffman_code/huffman"
echo "  - Fork:       ./Huffman_code_fork/huffman"
echo "  - Pthreads:   ./Huffman_code_pthreads/huffman"