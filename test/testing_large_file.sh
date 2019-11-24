#!/bin/sh

# make fresh virtual disk with 4 data block size 
./fs_make.x disk.fs 4

# Create a large file (larger than the maximum data block in disk.fs): 17 block size 
for i in $(seq -w 1 10000); do echo "string" >> file;done

# Add the large file to disk : only add 3 block size in file 
./fs_ref.x add disk.fs file >ref.stdout 2>ref.stderr
./fs_ref.x cat disk.fs file >ref.stdout 2>ref.stderr
./fs_ref.x stat disk.fs file >ref.stdout 2>ref.stderr
./fs_ref.x rm disk.fs file 

# Compared to our result 
./test_fs.x add disk.fs file >lib.stdout 2>lib.stderr
./test_fs.x cat disk.fs file >lib.stdout 2>lib.stderr
./test_fs.x stat disk.fs file >lib.stdout 2>lib.stderr
./test_fs.x rm disk.fs file 

# put output files into variables
REF_STDOUT=$(cat ref.stdout)
REF_STDERR=$(cat ref.stderr)

LIB_STDOUT=$(cat lib.stdout)
LIB_STDERR=$(cat lib.stderr)

# compare stdout
if [ "$REF_STDOUT" != "$LIB_STDOUT" ]; then
    echo "Stdout outputs don't match..."
    diff -u ref.stdout lib.stdout
else
    echo "Stdout outputs match!"
fi

# compare stderr
if [ "$REF_STDERR" != "$LIB_STDERR" ]; then
    echo "Stderr outputs don't match..."
    diff -u ref.stderr lib.stderr
else
    echo "Stderr outputs match!"
fi

# clean
rm disk.fs
rm ref.stdout ref.stderr
rm lib.stdout lib.stderr
rm file