#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "ext2_fs.h"
#include "read_ext2.h"

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		printf("expected usage: ./runscan inputfile outputfile\n");
		exit(0);
	}
	// create the dir
	if (!opendir(argv[2]))
	{
		mkdir(argv[2], 0700);
	}

	int fd;

	fd = open(argv[1], O_RDONLY); /* open disk image */

	ext2_read_init(fd);

	struct ext2_super_block super;
	struct ext2_group_desc group;

	// example read first the super-block and group-descriptor
	read_super_block(fd, 0, &super);
	read_group_desc(fd, 0, &group);

	// printf("There are %u inodes in an inode table block and %u blocks in the idnode table\n", inodes_per_block, itable_blocks);
	// iterate the first inode block
	off_t start_inode_table = locate_inode_table(0, &group);
	// TODO: make it iterate the entire inode table
	for (unsigned int i = 0; i < super.s_inodes_per_group; i++)
	{
		// printf("inode %u: \n", i);
		struct ext2_inode *inode = malloc(sizeof(struct ext2_inode));
		read_inode(fd, 0, start_inode_table, i, inode);
		/* the maximum index of the i_block array should be computed from i_blocks / ((1024<<s_log_block_size)/512)
		 * or once simplified, i_blocks/(2<<s_log_block_size)
		 * https://www.nongnu.org/ext2-doc/ext2.html#i-blocks
		 */
		// unsigned int i_blocks = inode->i_blocks / (2 << super.s_log_block_size);
		// printf("number of blocks %u\n", i_blocks);
		// printf("Is directory? %s \n Is Regular file? %s\n",
		// 	   S_ISDIR(inode->i_mode) ? "true" : "false",
		// 	   S_ISREG(inode->i_mode) ? "true" : "false");

		char buffer[1024];
		lseek(fd, (off_t)BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
		read(fd, buffer, block_size);
		int is_jpg = 0;
		if (buffer[0] == (char)0xff &&
			buffer[1] == (char)0xd8 &&
			buffer[2] == (char)0xff &&
			(buffer[3] == (char)0xe0 ||
			 buffer[3] == (char)0xe1 ||
			 buffer[3] == (char)0xe8))
		{
			is_jpg = 1;
		}
		// printf("Is jpeg? %s\n", is_jpg ? "true" : "false");
		// if it is a directory
		if (S_ISREG(inode->i_mode) && is_jpg)
		{
			// this inode represents a regular JPG file
			uint filesize = inode->i_size;
			char *inode_name = malloc(256);
			snprintf(inode_name, 256, "%s/file-%d.jpg", argv[2], i);
			int file_inode = open(inode_name, O_WRONLY | O_TRUNC | O_CREAT, 0666);
			if (file_inode < 0)
			{
				return -1;
			}
			char *inode_number = malloc(256);
			snprintf(inode_number, 256, "%s/%d.jpg", argv[2], i);
			int file_ptr = open(inode_number, O_WRONLY | O_TRUNC | O_CREAT, 0666);
			if (file_ptr < 0)
			{
				return -1;
			}

			uint block_id = 0;
			uint bytes_read = 0;
			for(; bytes_read < filesize && bytes_read < block_size * EXT2_NDIR_BLOCKS; bytes_read += block_size){
				lseek(fd, BLOCK_OFFSET(inode->i_block[block_id]), SEEK_SET);
				read(fd, buffer, block_size);
				uint size_to_be_written = filesize - bytes_read;
				if(size_to_be_written > block_size){
					size_to_be_written = block_size;
					block_id++;
				}
				write(file_inode, buffer, size_to_be_written);
				write(file_ptr, buffer, size_to_be_written);
			}
			// read the indirect block
			// single indirect block
			if (inode->i_block[EXT2_IND_BLOCK] != 0)
			{
				uint single_buffer[256];
				lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_IND_BLOCK]), SEEK_SET);
				read(fd, single_buffer, block_size);
				for (block_id = 0; bytes_read < filesize && block_id < 256; bytes_read += block_size)
				{
					lseek(fd, BLOCK_OFFSET(single_buffer[block_id]), SEEK_SET);
					read(fd, buffer, block_size);
					uint size_to_be_written = filesize - bytes_read;
					if (size_to_be_written > block_size)
					{
						size_to_be_written = block_size;
						block_id++;
					}
					// printf("write %d bytes to file %d\n", size_to_be_written, i);
					write(file_ptr, buffer, size_to_be_written);
					write(file_inode, buffer, size_to_be_written);
				}
			}
			// double indirect block
			if (inode->i_block[EXT2_DIND_BLOCK] != 0)
			{
				uint single_buffer[256];
				uint double_buffer[256];
				lseek(fd, BLOCK_OFFSET(inode->i_block[EXT2_DIND_BLOCK]), SEEK_SET);
				read(fd, single_buffer, block_size);
				uint single_id;
				for (single_id = 0; bytes_read < filesize && single_id < 256; single_id++)
				{
					lseek(fd, BLOCK_OFFSET(single_buffer[single_id]), SEEK_SET);
					read(fd, double_buffer, block_size);
					for(block_id = 0; bytes_read < filesize && block_id < 256; bytes_read += block_size)
					{
						lseek(fd, BLOCK_OFFSET(double_buffer[block_id]), SEEK_SET);
						read(fd, buffer, block_size);
						uint size_to_be_written = filesize - bytes_read;
						if (size_to_be_written > block_size)
						{
							size_to_be_written = block_size;
							block_id++;
						}
						// printf("write %d bytes to file %d\n", size_to_be_written, i);
						write(file_ptr, buffer, size_to_be_written);
						write(file_inode, buffer, size_to_be_written);
					}
				}
			}
		}
		else
		{
			// this inode represents other file types
			// printf("%d: -------------Other file type--------------\n", i);
			continue;
		}

		// // if it is a regular file, copy it to the output dir
		// if (S_ISREG(inode->i_mode)) {
		// 	// read and parse the directory entries
		// 	struct ext2_dir_entry_2* entry =  (struct ext2_dir_entry_2*)(inode->i_block[0]);
		// 	char* filename = strdup(entry->name);
		// 	printf("filename %s\n", filename);
		// 	// open a new file
		// 	int temp_file = open(filename, O_WRONLY | O_TRUNC | O_CREAT, 0666); // TODO: change the name
		// 	uint file_size = inode->i_size;
		// 	// iterate the blocks
		// 	void* buffer = calloc(inode->i_size, 1);
		// 	// create a file of same size
		// 	write(temp_file, buffer, file_size);
		// }
		// print i_block numbers
		// struct ext2_dir_entry* dentry = (struct ext2_dir_entry_2*) & ( buffer[offset] ); 
		// for (unsigned int i = 0; i < EXT2_N_BLOCKS; i++)
		// {
		// 	if (i < EXT2_NDIR_BLOCKS) /* direct blocks */
		// 		printf("Block %2u : %u\n", i, inode->i_block[i]);
		// 	else if (i == EXT2_IND_BLOCK) /* single indirect block */
		// 		printf("Single   : %u\n", inode->i_block[i]);
		// 	else if (i == EXT2_DIND_BLOCK) /* double indirect block */
		// 		printf("Double   : %u\n", inode->i_block[i]);
		// 	else if (i == EXT2_TIND_BLOCK) /* triple indirect block */
		// 		printf("Triple   : %u\n", inode->i_block[i]);
		// }

		free(inode);
	}
	for(unsigned int i = 0; i < super.s_inodes_per_group; i++){
		struct ext2_inode *inode = malloc(sizeof(struct ext2_inode));
		read_inode(fd, 0, start_inode_table, i, inode);
		if (S_ISDIR(inode->i_mode))
		{
			char dbuffer[1024];
			lseek(fd, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
			read(fd, dbuffer, block_size);
			uint current_offset = 24;
			struct ext2_dir_entry_2 *dentry = (struct ext2_dir_entry_2 *)&(dbuffer[current_offset]);
			while (current_offset < block_size)
			{
				if (dentry->name_len == 0)
					break;
				char *name = malloc(256);
				strncpy(name, dentry->name, dentry->name_len);
				*(name + dentry->name_len) = '\0';
				char* numbered_name = malloc(256);
				snprintf(numbered_name, 256, "%s/%d.jpg", argv[2], dentry->inode);
				char* new_name = malloc(512); // double the size make sure it is enough
				snprintf(new_name,512,"%s/%s",argv[2],name);
				FILE *file;
				if((file = fopen(numbered_name, "r"))){
					fclose(file);
					rename(numbered_name, new_name);
				}
				// printf("Inode Number:%d Name of file: %s\n", dentry->inode, name);
				current_offset = current_offset + dentry->name_len + sizeof(dentry->inode) + sizeof(dentry->rec_len) + sizeof(dentry->name_len) + sizeof(dentry->file_type);
				// current_offset = current_offset + dentry->rec_len;
				// aligned to 4 bytes
				current_offset = (current_offset + 3) & ~3;
				dentry = (struct ext2_dir_entry_2 *)&(dbuffer[current_offset]);
			}
		}
	}
	close(fd);
}
