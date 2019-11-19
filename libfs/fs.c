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
	uint64_t signature;
	uint16_t total_blocks_num;
	uint16_t root_index;
	uint16_t data_start;
	uint16_t data_blocks_num;
	uint8_t  fat_blocks_num;
	uint8_t  paddings[4079];
} __attribute__((packed));

struct SuperBlock super;

struct FAT {
	uint16_t *arr;
} __attribute__((packed));

struct FAT fat;

struct Entry {
	uint8_t filename[FS_FILENAME_LEN];
	uint32_t size_file;
	uint16_t first_data_index;
	uint8_t  paddings[10];
}__attribute__((packed));

struct RootDirectory {
	struct Entry entry[FS_FILE_MAX_COUNT];
} __attribute__((packed));

struct RootDirectory rootdir;

struct File {
	uint8_t filename[FS_FILENAME_LEN];
	size_t offset;
};

struct FilesTable {
	int num_open; //initialized to 0
	struct File file[FS_OPEN_MAX_COUNT];
};

struct FilesTable files_table;

int fs_mount(const char *diskname)
{
	int retval = block_disk_open(diskname);
	if (retval == -1)
		return -1; // -1 if virtual disk file @diskname cannot be opened
	block_read(0, &super);
	if (1 + super.fat_blocks_num + 1 + super.data_blocks_num != super.total_blocks_num)
		return -1; //super(1) + FAT + root(1) + data == TOTAL
	if(super.total_blocks_num != block_disk_count())
		return -1;
	if (super.fat_blocks_num != super.data_blocks_num * 2 / BLOCK_SIZE)
		return -1; //calculation doesn't agree with super info
	if (super.fat_blocks_num + 1 != super.root_index)
		return -1; // super #0, FAT #1,2,3,4 --> root: 5
	if (super.root_index + 1 != super.data_start)
		return -1;

	// The huge fat array consists of num_data_blocks uint16_t elements
	fat.arr = (uint16_t*)malloc(super.data_blocks_num * sizeof(uint16_t));
	size_t i = 1;
	void *buffer = (void*)malloc(BLOCK_SIZE);
	for (; i < super.root_index; i++) {
		// for each (i-1)th fat block, loads
		// fat block offset starts at 1 instead of 0, so mapping is i-1
		block_read(i, buffer);
		memcpy(fat.arr + (i-1)*BLOCK_SIZE, buffer, BLOCK_SIZE);
	}
	if (fat.arr[0] != 0xFFFF)
		return -1; // The first entry of the FAT (entry #0) is always invalid is 0xFFFF.

	// load the root dir infos
	retval = block_read(super.root_index, &rootdir);
	if (retval == -1)
		return -1;
	return 0;
}

int fs_umount(void)
{
	int retval = block_write(0, &super);
	if (retval == -1)
		return -1;
	size_t i = 1;
	for (; i < super.root_index; i++) {
		// for each fat block, writes the block into
		// fat block offset starts at 1 instead of 0, so mapping is i-1
		retval = block_write(i, fat.arr + (i-1) * BLOCK_SIZE);
		if (retval == -1)
			return -1;
	}
	retval = block_write(super.root_index, &rootdir);
	if (retval == -1)
		return -1;

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
		//MAYBE i should start from 1 here??????
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
	if (filename == NULL || strlen(filename) > FS_FILENAME_LEN)
		return -1;
	// NEXT we check first before we create file
	int file_count = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (rootdir.entry[i].filename[0] != '\0') {
			file_count += 1; // if the entry isn't for empty file, file count incremented
		}
		if (strcmp((char*)rootdir.entry[i].filename, filename) == 0) {
			return -1; // file already exists
		}
	}
	if (file_count >= FS_FILE_MAX_COUNT)
		return -1; // the root directory already contains FS_FILE_MAX_COUNT files.
	//done checking, move forward for creation
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		//An empty entry is defined by the first character of the entry’s filename being equal to the NULL character.
		if (rootdir.entry[i].filename[0] == '\0') {
			memcpy(rootdir.entry[i].filename, filename, FS_FILENAME_LEN); // copy the file name
			rootdir.entry[i].size_file = 0; // the root dir has size of 0
			rootdir.entry[i].first_data_index = 0xFFFF;  // the first data starts from 0xFFFF
			block_write(super.root_index, &rootdir); // write the root directory into block
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
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		//An empty entry is defined by the first character of the entry’s filename being equal to the NULL character.
		if (rootdir.entry[i].filename[0] == '\0') {
			struct Entry cur = rootdir.entry[i];
			printf("\nfile: %s, size: %i, data_blk: %i", (char*)cur.filename, cur.size_file, cur.first_data_index);
		}
	}
	printf("\n");
	return 0;
}

int fs_open(const char *filename)
{
	if (filename == NULL)
		return -1; //-1 if @filename is invalid
	// check whether file exists in root directory
	int fd_find = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp((char*)rootdir.entry[i].filename, filename) == 0)
			fd_find = 1;
	}
	if (fd_find == -1)
		return -1; // there is no file named @filename to open
	// check whether we have over 32 files opened
	if (files_table.num_open == FS_OPEN_MAX_COUNT)
		return -1; // _OPEN_MAX_COUNT files currently open

	//after 3 checks we proceed to open the file
	int ret_fd = -1;
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
		if (files_table.file[i].filename[0] == '\0') {
			// if the filename first character is NULL, then it's empty file slot
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

int FAT_ind(size_t offset, uint16_t start_index) {
	int count_offset = 0;
	uint16_t data_index = start_index;
	while (data_index != 0xFFFF && count_offset != offset) {
		// while the data_index doesn't reach to the end of the file
		// AND meanwhile we haven't reached the offset position
		data_index = fat.arr[data_index]; // update data_index through block chain
		count_offset ++; // increment counts
	}
	return data_index;
}

int FAT_1stEmpty_ind() {
	int i;
	for (i = 1; i < super.data_blocks_num; i++){
		//i should definitely start from 1 here!
		if (fat.arr[i] == 0){
			return i;
		}
	}
	return -1;
}

int fs_write(int fd, void *buf, size_t count)
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
	uint16_t start_index = 0xFFFF;
	//now we get the first data index for file
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp((char*)rootdir.entry[i].filename, filename) == 0) {
			start_index = rootdir.entry[i].first_data_index;
			break;
		}
	}
	if (start_index == 0xFFFF)
		return -1; //fd not found, so weird
	if (offset == size && count != 0) //if offset is at the very end of the file
		return 0; //cannot read anything, return

	int start= FAT_ind(offset, start_index);

	return 0;
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
	uint16_t start_index = 0xFFFF;
	//now we get the first data index for file
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp((char*)rootdir.entry[i].filename, filename) == 0) {
			start_index = rootdir.entry[i].first_data_index;
			break;
		}
	}
	if (start_index == 0xFFFF)
		return -1; //fd not found, so weird
	if (offset == size && count != 0) //if offset is at the very end of the file
		return 0; //cannot read anything, return
	void *bounce_buffer = (void*)malloc(BLOCK_SIZE);
	for (size_t i = 0; i < super.data_blocks_num; i++) {
		block_read(i+super.data_start, bounce_buffer);

	}

	return 0;
}

