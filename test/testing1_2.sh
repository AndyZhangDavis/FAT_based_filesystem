#!/bin/sh

# make fresh virtual disk
./fs_make.x disk.fs 4096
# get fs_info from reference lib
./fs_ref.x info disk.fs >ref.stdout 2>ref.stderr
# get fs_info from my lib
./test_fs.x info disk.fs >lib.stdout 2>lib.stderr

# create 5 files onto host 
for i in $(seq -w 1 5); do echo "hello world" > test${i}; done
# add 5 files to disk.fs
for i in $(seq -w 1 5); do ./fs_ref.x add disk.fs test${i}; done
# remove file to disk.fs
./fs_ref.x rm disk.fs file03
./fs_ref.x info disk.fs >ref.stdout 2>ref.stderr
./fs_ref.x ls disk.fs >ref.stdout 2>ref.stderr
# add file 06 to disk.fs
./fs_ref.x add disk.fs test06
./fs_ref.x info disk.fs >ref.stdout 2>ref.stderr
./fs_ref.x ls disk.fs >ref.stdout 2>ref.stderr
# Remove all files in disk.fs and execute same operation in our program to compare the result 
for i in $(seq -w 1 6); do ./fs_ref.x rm disk.fs file${i}; done
# add 5 files to disk.fs
for i in $(seq -w 1 5); do ./test_fs.x add disk.fs test${i}; done
# remove file to disk.fs
./test_fs.x rm disk.fs file03
./test_fs.x info disk.fs >lib.stdout 2>lib.stderr
./test_fs.x ls disk.fs >lib.stdout 2>lib.stderr
# add file 06 to disk.fs
./test_fs.x add disk.fs test06
./test_fs.x info disk.fs >lib.stdout 2>lib.stderr
./fs_ref.x ls disk.fs >lib.stdout 2>lib.stderr
# remove all files
for i in $(seq -w 1 6); do ./test_fs.x rm disk.fs file${i}; done

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
for i in $(seq -w 1 6); do rm file${i}; done
