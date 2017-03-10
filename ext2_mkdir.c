#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "ext2_utils.h"


#define USAGE "Usage: %s <image file name> <absolute path on ext2 image>\n"

#define NUM_ARGUMENT_V 3

unsigned char *disk = NULL;


/*
 * Creates a directory with the given path.
 *
 * Returns EXIT_SUCCESS, if the directory was successfully created.
 *               ENOENT, if one or more entries in the path don't exist.
 *               EEXIST, if the directory to be created already exists.
 */
int create_directory(char *path) {
    
    // Ensure that file_path starts with a '/'
    if (!IS_PATH_ABSOLUTE(path)) {
        return ENOENT;
    }
    
    struct ext2_inode *i_table = get_inode_table();
    
    // The inode of the current directory
    unsigned int curr_inode_num = NUM(EXT2_ROOT_INO_IDX);
    struct ext2_inode *curr_inode = i_table + EXT2_ROOT_INO_IDX;
    
    char *token = strtok(path, DIR_DELIMITER);
    
    // Current Directory Entry in path
    struct ext2_dir_entry *curr_dir_entry = find_entry(curr_inode, token);
    
    while (token != NULL) {
        char *next_token = strtok(NULL, DIR_DELIMITER);
        
        if (curr_dir_entry == NULL) {
            
            // Path does not exist
            if (next_token != NULL) {
                return ENOENT;
            }
            
            // Everything good, create new directory
            struct ext2_dir_entry *new_entry =
                            create_dir_entry(curr_inode, UNDEFINED, token, EXT2_FT_DIR);
                            
            get_group_descriptor()->bg_used_dirs_count++;
            
            // Create entry for self (.) inside new directory
            struct ext2_inode *new_inode = i_table + INDEX(new_entry->inode);
            create_dir_entry(new_inode, new_entry->inode, CURRENT_DIR, EXT2_FT_DIR);
            
            // Create entry for parent directory (..) inside new directory
            create_dir_entry(new_inode, curr_inode_num, PARENT_DIR, EXT2_FT_DIR);
            
            return EXIT_SUCCESS;
        }
        
        // Reached end of path. Target already exists
        else if (next_token == NULL) {
            return EEXIST;
        }
            
        // One or more entries in the path is not a directory.
        else if (curr_dir_entry->file_type != EXT2_FT_DIR) {
            return ENOENT;
        }
            
        // Everything good, continue traversing path
        token = next_token;
        
        curr_inode_num = curr_dir_entry->inode;
        curr_inode =  i_table + INDEX(curr_inode_num);
        curr_dir_entry = find_entry(curr_inode, token);
    }
    
    // Path does not exist
    return ENOENT;
}


int main(int argc, char *argv[]) {
    
    if (argc != NUM_ARGUMENT_V) {
        fprintf(stderr, USAGE, argv[0]);
        return EXIT_FAILURE;
    }
    
    char *disk_image_path = argv[1];
    char *target_path = argv[2];
    
    disk = read_disk_image(disk_image_path);
    
    return create_directory(target_path);

}