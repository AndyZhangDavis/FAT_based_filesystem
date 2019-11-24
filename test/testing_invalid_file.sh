#!/bin/sh

# make fresh virtual disk
./fs_make.x disk.fs 4096

# create 2 file
echo "hello world!" > dummydummydummydummy
echo "hello world!" > dummy

# add invlid file
./fs_ref.x add disk.fs dummydummydummydummy >ref.stdout 2>ref.stderr

# remove non-existed file
./fs_ref.x add disk.fs filenotexisted >ref.stdout 2>ref.stderr

# add existed file will fail
./fs_ref.x add disk.fs dummy >ref.stdout 2>ref.stderr
./fs_ref.x add disk.fs dummy >ref.stdout 2>ref.stderr

# add file
./test_fs.x add disk.fs dummydummydummydummy >lib.stdout 2>lib.stderr

# remove non-existed file
./test_fs.x add disk.fs filenotexisted >lib.stdout 2>lib.stderr

# add existed file will fail
./test_fs.x add disk.fs dummy >lib.stdout 2>lib.stderr
./test_fs.x add disk.fs dummy >lib.stdout 2>lib.stderr


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
rm dummydummydummydummy
rm dummy