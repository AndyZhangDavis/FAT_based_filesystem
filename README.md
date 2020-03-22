# FAT_based_filesystem
*Waiyu Lam; Wenhao Su*    
*Instructors: Prof. Joël Porquet Lupine*

## Objectives 
1. Implementing an entire FAT-based filesystem software stack: from mounting and unmounting a formatted partition, to reading and writing files, and including creating and removing files.
2. Understanding how a formatted partition can be emulated in software and using a simple binary file, without low-level access to an actual storage device.

## Introduction 
The goal of this project is to implement the support of a very simple file system, ECS150-FS. This file system is based on a FAT (File Allocation Table) and supports up to 128 files in a single root directory. Exactly like real hard drives which are split into sectors, the virtual disk is logically split into blocks. The first software layer involved in the file system implementation is the block API and is provided to you. This block API is used to open or close a virtual disk, and read or write entire blocks from it. Above the block layer, the FS layer is in charge of the actual file system management. Through the FS layer, you can mount a virtual disk, list the files that are part of the disk, add or delete new files, read from files or write to files, etc.

## Design 
The layout of ECS150-FS on a disk is composed of four consecutive logical parts:

1. The Superblock is the very first block of the disk and contains information about the file system (number of blocks, size of the FAT, etc.)
2. The File Allocation Table is located on one or more blocks, and keeps track of both the free data blocks and the mapping between files and the data blocks holding their content.
3. The Root directory is in the following block and contains an entry for each file of the file system, defining its name, size and the location of the first data block for this file.
4. Finally, all the remaining blocks are Data blocks and are used by the content of files.    

The size of virtual disk blocks is 4096 bytes.

## Sources 
[ECS 150: Project #4 - File system](https://canvas.ucdavis.edu/courses/364183/files/folder/projects?preview=7091111)



