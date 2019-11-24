# Filesystem

The objectives of this programming project are:
1. **File Data Structure**: Implementing an entire FAT-based filesystem.
2. **Emulation of Low-Level Disk Access**

## Design
### Overall shared Data strcuture 
1. SuperBlock:  the first block of the file system while the internal members
   are corresponding to all blocks in specification. We use integer types that
   have exact byte length provided in stdint.h c library to ensure that each
   members have exactly block width indicated in the instruction in order for
   block_read() work properly. The following codes is to show how we declare our
   super block strucut : 
    ```c++
    struct SuperBlock {
            char signature[8];
            uint16_t total_blocks_num;
            uint16_t root_index;
            uint16_t data_start;
            uint16_t data_blocks_num;
            uint8_t  fat_blocks_num;
            uint8_t  paddings[4079];
        } __attribute__((packed));
    ```
    Our variable to keep track of super block and FS information is called :
    super 
2. FAT: contains a flat array, possibly spanning several blocks, which entries
   are composed of 16-bit unsigned words. 
3. Entries: the entries in the root directory array stored in the block. Same as
   superblock data structure, we use specific integer types to ensure the length
   of bytes that eaach members contains. This data structure has the following
   four members : filename; size of the file; index of the first data block ;
   padding 
4. RootDirectory: an array of 128 entries stored in the block following the FAT
5. File : this is the data structure to store information about the
   corresponding file such as file name and offset size 
6. FilesTable : a data structure to keep track of all the open files 

## Implementation Details
1. `fs_mount()`: To implement the mounting function, which makes the file system
   to load the specific disk "ready to use", we first verify that the virtual
   disk existed. For all the invalid implementation our function will return -1
   to indicate failure in mounting. The process continue to use `block_read(0,
   &super);` to read the frist block in disk. We also verify with the following
   scenarios: 1.  verify that the file system has the expected format 2. verify
   that the total_blocks_num equal to what block_dick_count() return 3. verify
   signature of super block . If there is no invalid implementation, we continue
   to load the FAT. In order to initalize the FAT, we first compute the total
   byte length we need for FAT `super.data_blocks_num * 2` and the total
   spanning block of FAT `(uint8_t )(total_bytes / BLOCK_SIZE) + ((total_bytes %
   BLOCK_SIZE) != 0);`. The latter operation is to include the case that the
   total byte length of FAT can not be divisible by the block size. Before
   loading the FAT from disk into buffer, we still verify that the indexing for
   all blocks are following in instructions. After that, we initialize our FAT
   array by using bouncing buffer so that we can mapping the correct address
   from disk to our FAT array: 
   ```c++
    /* The FAT has array attribute which consists of num_data_blocks 
    two bytes long data block indexes*/
    fat.arr = (uint16_t*)malloc(super.data_blocks_num * sizeof(uint16_t));
    // FAT start at block index # 1
    size_t i = 1;
    // Mapping the reading block to FAT : we use bounce buffer 
    void *buffer = (void*)malloc(BLOCK_SIZE);
    for (; i < super.root_index; i++) {
        // for each (i-1)th fat block, loads
        // fat block offset starts at 1 instead of 0, so mapping is i-1
        if (block_read(i, buffer) == -1)
            return -1;
        memcpy(fat.arr + (i-1)*BLOCK_SIZE, buffer, BLOCK_SIZE);
    }
    ```
    After we then initalized the root directory, and read in the data with the
    root directory  index from the superblock.  

2. `fs_unmount()`: Before we close the disk, we write out all the
   meta-information and file data onto disk by using `block_write()`. After
   that, we invoke `block_disk_close()` to close the opened disk. 

3. `fs_info()`: We implement our print statement based on the output
   corresponding exactly to the referecence  program including some information
   about mounted file system, such as
   `total_blocks_num`,`fat_blocks_num`,`root_index`,`data_start_index`,
   `data_blocks_num`,`fat_free_ratio` and `rdir_free_ratio`. In order to compute
   the fat free ratio, we counting the number of empty entry by iterating
   through our FAT table `fat.arr` and the `fat_free_ratio` is the ratio of
   total empty entries to total data block. Similarly, we compute the
   rdir_free_ratio by searching empty entries through the root directory array. 

4. `fs_create`: The first step we create a new file is to check if the new file
   name is valid. 
   ```c++
   if (filename == NULL || strlen(filename) > FS_FILENAME_LEN ||
   filename[strlen(filename)-1] != '\0') return -1;
    ```
    After that, we continue to increment our counter `file_count` to keep track
    of total running file existed in the root directory. During iteration, the
    process will return -1 if the filename already existed in the root
    directory. We check if there is any available space in root directory for
    new file. If the root directory has already been full, the process return
    -1. Otherwise, it will agian iterate through `rootdir` for the first avabile
    empty entry to create new file. At that point, we initialize the new file
    with its file name and empty data size. After all, we update the new root
    directory from the file system by `block_write(super.root_index, &rootdir);`

5. `fs_delete`: To implement this function, we first verify that the file to be
   deleted is existed and running. By searching the file name through root dir,
   if we find the matching file name, we updated the values at that entry with
   initial default value. Since the empty entry is defined by the first
   character of the entry’s filename being equal to the NULL character, we set
   the entry's file name as Null character. After that we use a temp varialbe to
   store the index of first data block at that point for later use. Then we set
   the `first_data_index` and `size_file` field to its specified empty value. We
   update the new root directory from the file system by `block_write` and then
   break the loop. Given the temp variable for the index of first data block, we
   can loop through our FAT variable and we set all values to 0 until we hit the
   FAT_EOC file  0xFFFF.
    ```c++
    //now we have the starting data index in FAT, clean!
        while (data_index != 0xFFFF) {
            // while the data_index doesn't reach to the end of the file
            uint16_t next_index = fat.arr[data_index];
            fat.arr[data_index] = 0;
            data_index = next_index;
        }
    ```
6. `fs_ls`: We traverse through our root directory array and output the content
   of each non-empty entries ( the first character of file name is not equal to
   Null character).Since the non-empty entries in this array is not continuous
   (  file might be deleted in between created files ), we should go through all
   the entries in root directory as before. After that, the information about
   the filled in entries is printed as : 
``` c++
    printf("file: %s, size: %i, data_blk: %i\n", (char*)cur.filename, cur.size_file, 
        cur.first_data_index);
```

## Testing Phase 1-2 : 
By testing the implementation of phase 1 and 2, we implement our testing in
`testing1_2.sh`. In this shell script, we create several virtual disk with
different name and size by `./fs_make.x _ _`. To take account for the invalid
disk mount/umount,we list the non-exist disk by and check if the output of
`fs_ref.x` and `test_fs.x` matched.  Also we create several files in host
directory including the file with invalid file name.  We then compared  
with our program and took account of valid file names to the reference program.