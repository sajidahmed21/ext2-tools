#include <string.h>
#include "ext2.h"


#define DIR_DELIMITER "/"
#define CURRENT_DIR "."
#define PARENT_DIR ".."

#define DIR_DELIMITER_CHAR '/'
#define IS_PATH_ABSOLUTE(PATH) ((PATH[0] == DIR_DELIMITER_CHAR))

#define UNDEFINED 0

#define TRUE 1
#define FALSE 0

#define CHAR_BIT 8 // Number of bits in a char

#define DISK_BLK_SIZE 512

#define NUM_DISK_BLKS(CURR, DELTA) \
                                     \
                        (((CURR * DISK_BLK_SIZE) + (DELTA)) / DISK_BLK_SIZE)


#define NUM_DIRECT_PTRS 12

#define NUM_INDIRECT_PTRS 1

#define DIR_ENTRY_ALIGNMENT 4

#define INDEX(NUM) ((NUM) - 1)

#define NUM(INDEX) ((INDEX) + 1)

#define BLOCK_START(DISK, BLOCK_NUM) ((DISK) + (BLOCK_NUM) * EXT2_BLOCK_SIZE)

#define BLOCK_END(BLOCK_PTR) ((BLOCK_PTR) + EXT2_BLOCK_SIZE)

#define IS_IN_USE(BYTE, BIT) ((BYTE) & (1 << BIT))

unsigned char *read_disk_image(char *path);

struct ext2_super_block *get_super_block();

struct ext2_group_desc *get_group_descriptor();

struct ext2_inode *get_inode_table();

unsigned char *get_block_bitmap();

unsigned char *get_inode_bitmap();

int get_blocks_count();

int get_inodes_count();

int allocate_block();

void set_inode_in_use(unsigned int inode_num);

void set_block_in_use(unsigned int block_num);

int is_inode_in_use(unsigned int inode_num);

int is_block_in_use(unsigned int block_num);

void unlink_inode(unsigned int inode_num);

int get_name_len(char *name);

char *get_file_name(char *path);

int get_actual_dir_entry_len(struct ext2_dir_entry *entry);

struct ext2_dir_entry *find_entry(struct ext2_inode *dir_inode, char *name);

struct ext2_dir_entry *find_entry_in_inode(unsigned int inode_num,
                                           char *name);

struct ext2_dir_entry *create_dir_entry(struct ext2_inode *dir_inode,
                                        unsigned int link_inode,
                                        char *name, unsigned char file_type);