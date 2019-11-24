#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include "disk.h"
#include "fs.h"


struct SuperBlock {
	char signature[8];
	uint16_t total_blocks_num;
	uint16_t root_index;
	uint16_t data_start;
	uint16_t data_blocks_num;
	uint8_t  fat_blocks_num;
	uint8_t  paddings[4079];
} __attribute__((packed));

struct FAT {
	uint16_t *arr;
} __attribute__((packed));


struct Entry {
	uint8_t filename[FS_FILENAME_LEN];
	uint32_t size_file;
	uint16_t first_data_index;
	uint8_t  paddings[10];
}__attribute__((packed));

struct RootDirectory {
	struct Entry entry[FS_FILE_MAX_COUNT];
} __attribute__((packed));


struct File {
	uint8_t filename[FS_FILENAME_LEN];
	size_t offset;
};

struct FilesTable {
	int num_open; //initialized to 0
	struct File file[FS_OPEN_MAX_COUNT];
};

struct FilesTable files_table;
struct RootDirectory rootdir;
struct SuperBlock super;
struct FAT fat;

int fs_mount(const char *diskname)
{
	// try to open the disk 
	if (block_disk_open(diskname) == -1){
		return -1; // -1 if virtual disk file @diskname cannot be opened
	}
	// Read the first block of the disk : super block 
	block_read(0, &super);
	
	// error checking: verify that the file system has the expected format
	if (1 + super.fat_blocks_num + 1 + super.data_blocks_num != super.total_blocks_num)
		return -1; // super(1) + FAT + root(1) + data == TOTAL
	// error checking : verify that the total_blocks_num equal to what block_dick_count() return
	if(super.total_blocks_num != block_disk_count())
		return -1;
	// error checking : verify signature of super block 
    if (memcmp("ECS150FS", super.signature, 8) != 0)
        {return -1;}
	
	// The size/byte length of the FAT
	int total_bytes = super.data_blocks_num * 2;
	// Total spanning block for FAT : ceiling make sure enough space for all indexe
	uint8_t ceilVal = (uint8_t )(total_bytes / BLOCK_SIZE) + ((total_bytes % BLOCK_SIZE) != 0);
	// error checking : verify that block indexing is following specification
	if (super.fat_blocks_num != ceilVal)
		return -1; // ceil of total_bytes / BLOCK_SIZE != fat num
	if (super.fat_blocks_num + 1 != super.root_index)
		return -1; // super #0, FAT #1,2,3,4 --> root: 5
	if (super.root_index + 1 != super.data_start)
		return -1;

	// The FAT has array attribute which consists of num_data_blocks two bytes long data block indexes
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
	// The first entry of the FAT (entry #0) is always invalid is 0xFFFF
	if (fat.arr[0] != 0xFFFF)
		return -1; 

	// load the root dir infos
	if (block_read(super.root_index, &rootdir) == -1)
		return -1;

	return 0;
}

int fs_umount(void)
{
	if (block_write(0,&super)==-1){
		return -1;
	}
	size_t i = 1;
	for (; i < super.root_index; i++) {
		// for each fat block, writes the block into
		// fat block offset starts at 1 instead of 0, so mapping is i-1
		if (block_write(i, fat.arr + (i-1) * BLOCK_SIZE) == -1){
			return -1;
		}
	}
	if (block_write(super.root_index, &rootdir) == -1){
		return -1;
	}
	return block_disk_close();
}

int fs_info(void)
{
	printf("FS Info:\n");
	printf("total_blk_count=%i\n",super.total_blocks_num);
	printf("fat_blk_count=%i\n",super.fat_blocks_num);
	printf("rdir_blk=%i\n",super.root_index);
	printf("data_blk=%i\n",super.data_start);
	printf("data_blk_count=%i\n",super.data_blocks_num);

	int num_free_fat = 0;
	for(int i = 0; i < super.data_blocks_num; i++){
		if(fat.arr[i] == 0)
			num_free_fat ++;
	}
	printf("fat_free_ratio=%d/%d\n", num_free_fat,super.data_blocks_num);

	int num_free_root = 0;
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if (rootdir.entry[i].filename[0] == '\0')
			num_free_root++;
	}
	printf("rdir_free_ratio=%d/%d\n", num_free_root, FS_FILE_MAX_COUNT);
	return 0;
}

int fs_create(const char *filename)
{
	// Verify that filename to create is valid 
	if (filename == NULL || strlen(filename) > FS_FILENAME_LEN )
		return -1;
	// NEXT we check first before we create file
	int file_count = 0;
	// Searching  through root directory
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (rootdir.entry[i].filename[0] != '\0') {
			file_count += 1; // if the entry isn't for empty file, file count incremented
		}
		if (strcmp((char*)rootdir.entry[i].filename, filename) == 0) {
			return -1; // file already exists
		}
	}
	// The root directory already contains FS_FILE_MAX_COUNT files.
	if (file_count >= FS_FILE_MAX_COUNT)
		return -1; 
	//After checking, move forward for creation
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		//An empty entry is defined by the first character of the entry’s filename being equal to the NULL character.
		if (rootdir.entry[i].filename[0] == '\0') {
			memcpy(rootdir.entry[i].filename, filename, FS_FILENAME_LEN); // copy the file name
			rootdir.entry[i].size_file = 0; // the root dir has size of 0
			rootdir.entry[i].first_data_index = 0xFFFF;  // the first data starts from 0xFFFF
			break;
		}
	}

	return 0;
}

int fs_delete(const char *filename)
{
	if (filename == NULL)
		return -1;
	uint16_t data_index = 0xFFFF;
	int file_found = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp((char*)rootdir.entry[i].filename, filename) == 0) {
			file_found = 1; // find the file!
			data_index = rootdir.entry[i].first_data_index; // find the first data index
			//An empty entry is defined by the first character of the entry’s filename being equal to the NULL character.
			rootdir.entry[i].filename[0] = '\0'; //set the entry name to NULL
			rootdir.entry[i].size_file = 0; // cleans
			rootdir.entry[i].first_data_index = 0xFFFF; // cleans

			block_write(super.root_index, &rootdir);
			break;
		}
	}
	if (file_found == -1) // file not found
		return -1;

	//now we have the starting data index in FAT, clean!
	while (data_index != 0xFFFF) {
		// while the data_index doesn't reach to the end of the file
		uint16_t next_index = fat.arr[data_index];
		fat.arr[data_index] = 0;
		data_index = next_index;
	}

	return 0;
}

int fs_ls(void)
{
	printf("FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		//An empty entry is defined by the first character of the entry’s filename being equal to the NULL character.
		if (rootdir.entry[i].filename[0] != '\0') {
			// if the file entry isn't null, we access the struct
			struct Entry cur = rootdir.entry[i];
			printf("file: %s, size: %i, data_blk: %i\n", (char*)cur.filename, cur.size_file, cur.first_data_index);
		}
	}
	return 0;
}

int fs_open(const char *filename)
{
	// Error verification: @filename is valid
	if (filename == NULL || strnlen(filename, FS_FILENAME_LEN) >= FS_FILENAME_LEN)
		return -1; 

	// Error verification:: check whether file exists in root directory
	int fd_find = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp((char*)rootdir.entry[i].filename, filename) == 0)
			fd_find = 1;
	}

	if (fd_find == -1)
		return -1; // there is no file named @filename to open

	// Error verification: check whether we have over 32 files opened
	if (files_table.num_open == FS_OPEN_MAX_COUNT)
		return -1; // _OPEN_MAX_COUNT files currently open

	//after error checking we proceed to open the file
	int ret_fd = -1;
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
		// if the filename first character is NULL, then it's empty file slot
		if (files_table.file[i].filename[0] == '\0') {
			files_table.num_open++; //increament open files count
			memcpy(files_table.file[i].filename, filename, FS_FILENAME_LEN); // copy the file name
			files_table.file[i].offset = 0; //set offset to 0
			ret_fd = i; // get the fd to return
			break;
		}
	}
	if (ret_fd == -1)
		return -1; //if ret fd isn't updated at all
	return ret_fd;
}

int fs_close(int fd)
{
	if (fd > 31 || fd < 0)
		return -1; // out of bounds
	if (files_table.file[fd].filename[0] == '\0')
		return -1; // not currently opened
	// now we proceed to reset
	files_table.file[fd].filename[0] = '\0'; // change the file name to NULL
	files_table.file[fd].offset = 0; // reset offset

	files_table.num_open--; // decrease the open count
	return 0;
}

int fs_stat(int fd)
{
	if (fd > 31 || fd < 0)
		return -1; // out of bounds
	if (files_table.file[fd].filename[0] == '\0')
		return -1; // not currently opened
	char *filename = (char*)files_table.file[fd].filename;

	int ret_size = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp((char*)rootdir.entry[i].filename, filename) == 0) {
			// if the name matches
			ret_size = rootdir.entry[i].size_file;
		}
	}
	return ret_size;
}

int fs_lseek(int fd, size_t offset)
{
	int size = fs_stat(fd);
	if (size == -1)
		return -1;
	if (offset > size)
		return -1;
	files_table.file[fd].offset = offset;
	return 0;
}

int data_ind(size_t offset, uint16_t file_start) {
	//return index of data block according to the offset
	// file_start is the starting fat index
	// offset is the file current offset
	int count_offset = BLOCK_SIZE - 1; //initial boundary is the end of the file
	uint16_t data_index = file_start;
	while (data_index != 0xFFFF && count_offset < offset) {
		// while the data_index doesn't reach to the end of the file
		// AND meanwhile we haven't reached the offset position
		if (fat.arr[data_index] == 0xFFFF)
			return data_index + super.data_start; //return index cannot be 0xFFFF
		data_index = fat.arr[data_index]; // update data_index through block chain
		count_offset += BLOCK_SIZE; // increment counts
	}
	return data_index + super.data_start; //return data index + offset
}

uint16_t fat_1stEmpty_ind() {
	//claim the first empty availble fat entry, and change the value of it to 0XFFFF
	uint16_t i;
	for (i = 1; i < super.data_blocks_num; i++){
		//i should definitely start from 1 here!
		if (fat.arr[i] == 0){
			fat.arr[i] = 0xFFFF; //set the entry value to FAT_EOC
			return i;
		}
	}
	return (uint16_t)0xFFFF;
}

int fs_write(int fd, void *buf, size_t count)
{
	if (count < 0 || buf == NULL)
		return -1;
	if (count == 0)
		return 0;
	if (fd > 31 || fd < 0)
		return -1; // out of bounds
	if (files_table.file[fd].filename[0] == '\0')
		return -1; // not currently opened

	char *filename = (char*)files_table.file[fd].filename; //get the filename
	size_t offset = files_table.file[fd].offset;
	int size = fs_stat(fd); //get fd size
	uint16_t file_start = 0xFFFF;
	//now we get the first data index for file
	int root_index = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp((char*)rootdir.entry[i].filename, filename) == 0) {
			file_start = rootdir.entry[i].first_data_index;
			root_index = i;
			break;
		}
	}
	if (root_index == -1 || file_start == 0)
		return -1; // file not found or file start with FAT 0, so weird

	if (size == 0) {
		//the file is empty with no allocated data block
		uint16_t next_fat_index = fat_1stEmpty_ind(); 
		if (next_fat_index == 0xFFFF){
			return 0;
		}else{
			rootdir.entry[root_index].first_data_index  = next_fat_index;
		}
		file_start = rootdir.entry[root_index].first_data_index; //update our file_start
	}

	void *bounce_buffer = (void*)malloc(BLOCK_SIZE);
	int data_index = data_ind(offset, file_start);
	block_read((size_t )data_index, bounce_buffer);
	size_t bounce_offset = offset % BLOCK_SIZE; //get local bounce offset for the bounce buf(first data block)
	int size_incrementing_flag = 0;
	int count_byte = 0;
	for (size_t i = 0; i < count; i++, bounce_offset++, offset++) {
		files_table.file[fd].offset = offset; //update file table current offset
		//for every write operation, we incremented buf offset i, bounce(in_block) offset, file offset
		if (offset >= size) // if reach the end of the file and we are still writing
			size_incrementing_flag = 1; //start to incrementing the size of the file

		if (bounce_offset >= BLOCK_SIZE) {
			bounce_offset = 0; //reset bounce offset to the beginning
			if (size_incrementing_flag == 1) {
				// if we're incrementing size of the file and need new space
				uint16_t next_fat_index = fat_1stEmpty_ind(); //get current data block index
				if (next_fat_index == 0xFFFF) {
					return count_byte; // return if we have no next data block
				}
				fat.arr[data_index] = next_fat_index; //update fat array linked structure, cur points to next
				data_index = next_fat_index + super.data_start; //data_index = fat index + offset
			} else {
				data_index = data_ind(offset, file_start); //get next data block index
			}
			block_read((size_t )data_index, bounce_buffer); //get new bounce buffer
		}
		memcpy(bounce_buffer + bounce_offset, buf + i, 1); //copy 1 byte each write: buf -> bounce
		count_byte++;
		// Potential Performance improvements: writing in 2 cases: reaching the final count OR bounce_offset is 4095
		// which is: if (i == count - 1 || bounce_offset == BLOCK_SIZE - 1)
		block_write((size_t )data_index, bounce_buffer);
		if (size_incrementing_flag == 1){ // if we are writing new bytes to the file
			rootdir.entry[root_index].size_file ++; // increment the size file
			block_write(super.root_index, &rootdir);
		}
	}
	return count_byte;
}

int fs_read(int fd, void *buf, size_t count)
{
	if (count < 0)
		return -1;
	if (fd > 31 || fd < 0)
		return -1; // out of bounds
	if (files_table.file[fd].filename[0] == '\0')
		return -1; // not currently opened

	char *filename = (char*)files_table.file[fd].filename; //get the filename
	size_t offset = files_table.file[fd].offset;

	int size = fs_stat(fd); //get fd size
	uint16_t file_start = 0xFFFF;

	//now we get the first data index for file
	int root_index = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp((char*)rootdir.entry[i].filename, filename) == 0) {
			file_start = rootdir.entry[i].first_data_index;
			root_index = i;
			break;
		}
	}

	if (size == 0) //if the file is empty
		return 0; //cannot read anything, return 0
	if (root_index == -1 || file_start == 0xFFFF || file_start == 0)
		return -1; //fd is not found OR starts with 0th fat, so weird
	if (offset == size && count != 0) //if offset is at the very end of the file
		return 0; //cannot read anything, return

	void *bounce_buffer = (void*)malloc(BLOCK_SIZE);
	int start_data_index = data_ind(offset, file_start);
	block_read((size_t )start_data_index, bounce_buffer);
	
	size_t bounce_offset = offset % BLOCK_SIZE; //get local bounce offset for the bounce buf(first data block)
	int count_byte = 0;
	for (size_t i = 0; i < count; i++, bounce_offset++, offset++) {
		//for every read operation, we incremented buf offset i, bounce(in_block) offset, file offset
		files_table.file[fd].offset = offset; //update file table current offset
		if (offset >= size) {
			return count_byte; //reach the end of the file
		}
		if (bounce_offset >= BLOCK_SIZE) { // if offset is >= 4096, aka > 4095
			bounce_offset = 0; //reset bounce offset to the beginning
			int next_data_index = data_ind(offset, file_start); //get next data block index
			if (next_data_index == 0xFFFF) {
				return count_byte; // return if we have no next data block
			}
			block_read((size_t )next_data_index, bounce_buffer); //get new bounce buffer
		}
		memcpy(buf + i, bounce_buffer + bounce_offset, 1); //copy 1 byte each read
		count_byte++;
	}
	return count_byte;
}