#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ext2_utils.h"

#define USAGE "Usage: %s <image file name> <file on native OS> "\
                        "<path on ext2 image>\n"

#define NUM_ARGUMENT_V 4
#define READ_BINARY "rb"

unsigned char *disk = NULL;


/*
 * Returns 1 if and only if the file at the given `path` is a regular file.
 *
 * Attribution: http://stackoverflow.com/questions/4553012/
                           checking-if-a-file-is-a-directory-or-just-a-file
 */
int is_regular_file(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}


/*
 * Opens and returns the file at the given `path` on the native OS.
 */
FILE *open_file(char *path) {
    
    // Ensure that the source file is a regular file
    if (!is_regular_file(path)) {
        exit(ENOENT);
    }
    
    FILE *file = fopen(path, READ_BINARY);
    if (file == NULL) {
        exit(ENOENT);
    }
    
    return file;
}

/*
 * Creates a file by the given name at the given path.
 *
 * Returns the directory entry for the newly created file, if it was created.
 */
struct ext2_dir_entry *create_target_file(char *path, char *name) {
    
    // Ensure that file_path starts with a '/'
    if (!IS_PATH_ABSOLUTE(path)) {
        exit(ENOENT);
    }
    
    struct ext2_inode *i_table = get_inode_table();
    
    // The inode of the current directory
    struct ext2_inode *curr_inode = i_table + EXT2_ROOT_INO_IDX;
    
    int path_len = strlen(path) + 1;  // Length of path (including null byte)
    char *rem_path = path + 1;        // Remaining path
    
    char tokenized_path[path_len];
    strncpy(tokenized_path, path, path_len);
    
    char *token = strtok(tokenized_path, DIR_DELIMITER);
    if (token != NULL)
        rem_path += strlen(token);
    
    // Current Directory Entry in path
    struct ext2_dir_entry *curr_dir_entry = find_entry(curr_inode, token);
    
    while (curr_dir_entry != NULL) {
        // Shift rem_path to omit the directory delimiter
        if (curr_dir_entry->file_type == EXT2_FT_DIR && rem_path[0] == '/')
            rem_path++;
        
        // Curr entry is not a 'Directory'
        if (curr_dir_entry->file_type != EXT2_FT_DIR) {
            
            // One (or more) of the entries in the path is not a 'Directory'
            if (strlen(rem_path) != 0) {
                exit(ENOENT);
            }
            // A file or link already exists by this name
            exit(EEXIST);
        }
        
        // Get next token in path
        token = strtok(NULL, DIR_DELIMITER);
        if (token != NULL)
            rem_path += strlen(token);
        
        curr_inode = i_table + (INDEX(curr_dir_entry->inode));
        curr_dir_entry = find_entry(curr_inode, token);
    }
    
    if (strlen(rem_path) != 0) {
        exit(ENOENT);
    }
    
    if (token != NULL)
        name = token;
    
    return create_dir_entry(curr_inode, UNDEFINED, name, EXT2_FT_REG_FILE);
}


/*
 * Copies data from the given file into the given inode.
 */
void copy_data(struct ext2_inode *inode, FILE *src) {
    
    unsigned int bytes_read;
    unsigned char buf[EXT2_BLOCK_SIZE];
    
    int block_ptr_index = 0;
    
    // Write data to the direct blocks
    while (block_ptr_index < NUM_DIRECT_PTRS &&
           (bytes_read = fread(buf, 1, EXT2_BLOCK_SIZE, src)) > 0) {
        
        // Allocate block
        unsigned int block_num = allocate_block();
        inode->i_block[block_ptr_index++] = block_num;
        inode->i_blocks = NUM_DISK_BLKS(inode->i_blocks, EXT2_BLOCK_SIZE);
        
        // Copy data into block
        unsigned char *block = disk + (block_num * EXT2_BLOCK_SIZE);
        memcpy(block, buf, bytes_read);
        
        inode->i_size += bytes_read;
    }
    
    // Return if all data have been written to the file
    if ((bytes_read = fread(buf, 1, EXT2_BLOCK_SIZE, src)) <= 0) {
        return;
    }
    
    // Allocate indirect block and write remaining data
    unsigned int indirect_block_num = allocate_block();
    inode->i_block[block_ptr_index++] = indirect_block_num;
    inode->i_blocks = NUM_DISK_BLKS(inode->i_blocks, EXT2_BLOCK_SIZE);
    
    // Current and starting position in indirect block
    unsigned int *indirect_block = (unsigned int *)
                                     BLOCK_START(disk, indirect_block_num);
    
    int max_indirect_blocks = EXT2_BLOCK_SIZE / sizeof(unsigned int);
    int num_indirect_blocks = 0;
    
    do {
        // Allocate direct block
        unsigned int direct_block_num = allocate_block();
        *(indirect_block++) = direct_block_num;
        inode->i_blocks = NUM_DISK_BLKS(inode->i_blocks, EXT2_BLOCK_SIZE);
        
        // Copy data into block
        unsigned char *block = BLOCK_START(disk, direct_block_num);
        memcpy(block, buf, bytes_read);
        
        inode->i_size += bytes_read;
        
        num_indirect_blocks++;
        
    } while (num_indirect_blocks < max_indirect_blocks &&
             (bytes_read = fread(buf, 1, EXT2_BLOCK_SIZE, src)) > 0);
}


int main(int argc, char *argv[]) {
    
    if (argc != NUM_ARGUMENT_V) {
        fprintf(stderr, USAGE, argv[0]);
        return EXIT_FAILURE;
    }
    
    char *disk_image_path = argv[1];
    char *src_path = argv[2];
    char *target_path = argv[3];
    
    disk = read_disk_image(disk_image_path);
    
    FILE *src_file = open_file(src_path);
    char *src_name = get_file_name(src_path);
    
    // Create target file
    struct ext2_dir_entry *target = create_target_file(target_path, src_name);
    struct ext2_inode *inode = get_inode_table() + INDEX(target->inode);
    
    // Copy data into target file
    copy_data(inode, src_file);
    
    fclose(src_file);
    
    return EXIT_SUCCESS;
}