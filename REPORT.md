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
   corresponding file such as file name and current offset for the file
6. FilesTable : a data structure to keep track of all the open files (file
   descriptor). We have array of open files object and a integer number to
   indicate the total number of openning files. 

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
7. `fs_open()`: The first step before we open the file is to handle the error
   case. We need to verify that if the filename is valid, if there are maximum
   files openning and if the file does not exist in disk.After that, we traverse
   through our file table `files_table.file[i]` to find the first empty file
   descriptor. In our implementation, we are using static variable and the Null
   character file names indicates that the current file descriptor is empty. At
   that point, we increment the number of open files by one and copy the file
   name and initilize 0 offset. The function will return the index of current
   point in file table.
   ```c++
    if (files_table.file[i].filename[0] == '\0') {
                files_table.num_open++; //increament open files count
                // copy the file name
                memcpy(files_table.file[i].filename, filename, FS_FILENAME_LEN); 
                files_table.file[i].offset = 0; //set offset to 0
                ret_fd = i; // get the fd to return
                break;
        }
    ```

8. `fs_close()`: this function fist check if the file descriptor input parameter
   is valid (if out of bound) or if the file descriptor does not have opened
   file. After that, we set the `files_table.file[fd].filename[0]` to null
   character which indicate the empty entry and reset the offset to 0. Also we
   decrement the number of open file and then return. 

9. `fs_stat()`: the function `fs_stat()` returns the file size corresponding to
   the file descriptor. We first iterate through our file table to find the
   pairing file name at the file descriptor. Then we iterating through our root
   dir to get the information (size) of the file. 

10. `fs_lseek()`: the function allows user to move the offset to the file. We
    set the offset by first finding the file descriptor at file table and sets
    the offset of the fd to the @offset.

11. `fs_read()`: we first do the preliminary checks to determine that if the
    file was open in file table or if the reading bytes and file descriptor are
    valid. After that, we continue to read the file. The first step is to load
    the filename , current offset and size of the file descriptor from file
    table. In order to read data block by block on our FAT correctly, we need to
    search the first data block index and root index of the file in root
    directory, which is the starting point for the file data. Then we determined
    the actual starting block in this read operation given the specific offset
    point by `data_ind`. The `data_ind` return index of data block according to
    the offset and declared as the following: 
    ```c++
        int count_offset = BLOCK_SIZE - 1; 
        uint16_t data_index = file_start;
        while (data_index != 0xFFFF && count_offset < offset) {
            if (fat.arr[data_index] == 0xFFFF)
                return data_index + super.data_start; 
            data_index = fat.arr[data_index]; 
            count_offset += BLOCK_SIZE; 
        }
        return data_index + super.data_start; 
    ```    
    The actual block number is equal to the index in FAT + the actual starting
    data block index . With the correct data block index, the next step is to
    read from the starting block accordning to offset. We initialize a bounce
    buffer and call `block_read((size_t )start_data_index, bounce_buffer);` to
    read the block to `bounce_buffer`. Since the offset can be non-zero and
    might not be the starting point of the current block, we needed to read from
    an local offset within the block. We divided the given offset by the
    BLOCK_SIZE and the remainder is the actual local offset we need within the
    first data block and store as `bounce_offset`. Given the local offset point
    we need to start on, we use for loop to read the total given number of bytes
    from there byte by byte and for every read operation, we incremented buf
    offset i, bounce(in_block) offset, file offset by one. At the same time we
    update the offset of the file descriptor to current reading offset. We have
    the variable `size` which is the total of the current file so that we can
    check if the current file offset reach the end of the file. Since the
    bounce_offset is to keep trakc of the local offset in the current data
    block, we need to update the value to 0 if the offset reach the end of the
    block. We find the next data block index by `int next_data_index =
    data_ind(offset, file_start);` and update the bounce buffer by read the new
    data block.
    ```c++
        for (size_t i = 0; i < count; i++, bounce_offset++, offset++) {..}
    ```
    We use `memcpy` to extract the data from bounce buffer to the `buf` as 
    ```c++
    memcpy(buf + i, bounce_buffer + bounce_offset, 1);
    ```
    Once reading is complete it returns the number of bytes read. If the read
    requests more bytes than in the file we simply return the number of bytes
    until the EOF is reached.  
12. `fs_write()`: Similar as the reading operation,we first do the prelimitary
    check and continue to load the data with correct offset mapping. We find the
    first starting data block and continue write operation at that point.
    Instead of reading the data, we need to overwrite the data with given
    `count` bytes starting at that point. However, in case of writing to empty
    file without any data block, the index of the first data block be FAT_EOC.We
    should first allocate the data block for this file. We initiaze the first
    data block for the file in root directory by `fat_1stEmpty_ind()`. This
    function searched the index of first free data blocks (value 0) in FAT array
    and update the entry value to 0xFFFF and return the current index . If there
    is no more free data block in FAT, then it will return 0xFFFF which
    indicates that there is no more data block for writing new data. After that,
    given the first starting data block index of this file, we did the same
    thing as reading operation to load the first data block into buffer.And then
    writing the data byte by byte. The reason why we need the current data block
    buffer because we want to resume the overall data block if the current
    offset is not at the beginning of the block. By iteraing through data block
    byte by byte, we also need to increment the file size if the current offset
    is greater than the total file size. We write the given buf value to bounce
    buffer by `memcpy(bounce_buffer + bounce_offset, buf + i, 1);` and write the
    bounce buffer to the data block by `block_write((size_t )data_index,
    bounce_buffer);`. We repeated this process until all data required was  
    written or until we ran out of blocks.

## Testing
1. `testing_large_file.sh` : in this testing case, we create a large file that
   has 17 data block size in host. We create a disk that has only two block
   size. To add this large file into the disk, we can expect that it only write
   part of (2 block size) the large file into disk. Also we add another new file
   into the disk and we should expect the failure in adding because the disk has
   been full

2. `testing_create_remove.sh`: By testing the implementation of phase 1 and 2,
   we implement our testing in `testing1_2.sh`. In this shell script, we create
   several virtual disk with different name and size by `./fs_make.x _ _`. To
   take account for the invalid disk mount/umount,we list the non-exist disk by
   and check if the output of `fs_ref.x` and `test_fs.x` matched.  Also we
   create several files in host directory including the file with invalid file
   name.  We then compared with our program and took account of valid file names
   to the reference program.

3.




