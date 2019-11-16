#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>

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
	uint8_t filename[16];
	uint32_t size_file;
	uint16_t first_data_index;
	uint8_t  paddings[10];
}__attribute__((packed));

struct RootDirectory {
	struct Entry entry[128];
} __attribute__((packed));

struct RootDirectory rootdir;

int fs_mount(const char *diskname)
{
	int retval = block_disk_open(diskname);
	if (retval == -1)
		return -1; // -1 if virtual disk file @diskname cannot be opened
	block_read(0, &super);
	if(super.total_blocks_num != block_disk_count())
		return -1;
	if (super.fat_blocks_num != super.data_blocks_num * 2 / BLOCK_SIZE)
		return -1; //calculation doesn't agree with super info

	// The huge fat array consists of num_data_blocks uint16_t elements
	fat.arr = (uint16_t*)malloc(super.data_blocks_num * sizeof(uint16_t));
	size_t i = 1;
	for (; i < super.root_index; i++) {
		// for each (i-1)th fat block, loads
		// fat block offset starts at 1 instead of 0, so mapping is i-1
		retval = block_read(i, fat.arr + (i-1)*BLOCK_SIZE);
		if (retval == -1)
			return -1;
	}
	if (fat.arr[0] != 0xFFFF)
		return -1; // The first entry of the FAT (entry #0) is always invalid is 0xFFFF.

	// load the root dir infos from the giant block to the buffer then parse
	void *buffer = malloc(BLOCK_SIZE);
	retval = block_read(super.root_index, buffer);
	if (retval == -1)
		return -1;
	for (int entry_ind = 0; entry_ind < 128; entry_ind++) {
		// each entry is 32 bytes
		// memcopy 128 parts of buffer to each root directory entry
		memcpy(&(rootdir.entry[entry_ind]), buffer + entry_ind * 32, 32); //buffer + entry_ind * 32 is the buffer + offest
	}

	return 0;
}

int fs_umount(void)
{
	/*
	void *buffer = malloc(BLOCK_SIZE);
	memcpy(buffer, &super, BLOCK_SIZE);
	block_write(0, buffer);
	 */
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
		if(fat.arr[i] == 0)
			num_free_fat ++;
	}
	printf("fat_free_ratio=%d/%d\n", num_free_fat,super.data_blocks_num);

	int num_free_root = 0;
	for(int i = 0; i < 128; i++){
		if (rootdir.entry[i].filename[0] == '\0')
			num_free_root++;
	}

	printf("rdir_free_ratio=%d/%d\n", num_free_root, 128);
	return 0;
}

int fs_create(const char *filename)
{
	if (filename == NULL || strlen(filename) > 16)
		return -1;
	// NEXT we check first before we create file
	int file_count = 0;
	for (int i = 0; i < 128; i++) {
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
	for (int i = 0; i < 128; i++) {
		//An empty entry is defined by the first character of the entry’s filename being equal to the NULL character.
		if (rootdir.entry[i].filename[0] == '\0') {
			memcpy(rootdir.entry[i].filename, filename, 16); // copy the file name
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
	for (int i = 0; i < 128; i++) {
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
	for (int i = 0; i < 128; i++) {
		//An empty entry is defined by the first character of the entry’s filename being equal to the NULL character.
		if (rootdir.entry[i].filename[0] == '\0') {
			struct Entry cur = rootdir.entry[i];
			printf("\nfile: %s, size: %i, data_blk: %i", (char *) cur.filename, cur.size_file, cur.first_data_index);
		}
	}
	printf("\n");
	return 0;
}

int fs_open(const char *filename)
{

	return 0;
}

int fs_close(int fd)
{
	return 0;
}

int fs_stat(int fd)
{
	return 0;
}

int fs_lseek(int fd, size_t offset)
{
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	return 0;
}

int fs_read(int fd, void *buf, size_t count)
{
	return 0;
}

