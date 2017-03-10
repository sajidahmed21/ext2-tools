#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "ext2_utils.h"

#define USAGE "Usage: %s <image file name> [-s] <target> <link name>\n"

#define SYM_LINK_FLAG 's'

#define MIN_ARGUMENT_V 4
#define MAX_ARGUMENT_V 5

unsigned char *disk = NULL;


/*
 * Return TRUE if the format of the path and directory entry file_type match.
 * Return FALSE, otherwise.
 */
int path_terminator_valid(char *path, struct ext2_dir_entry *entry) {
    if (entry != NULL &&
        entry->file_type != EXT2_FT_DIR && path[strlen(path) - 1] == '/') {
        
        return FALSE;
    }
    
    return TRUE;
}


/*
 * Returns the directory entry for the file referred by the given `path`.
 */
struct ext2_dir_entry *find_dir_entry(char *path) {
    
    // Ensure that path starts with a '/'
    if (!IS_PATH_ABSOLUTE(path)) {
        exit(ENOENT);
    }
    
    struct ext2_inode *i_table = get_inode_table();
    
    // The inode of the current directory
    struct ext2_inode *curr_inode = i_table + EXT2_ROOT_INO_IDX;
    
    int path_len = strlen(path) + 1;  // Length of path (including null byte)
    
    char tokenized_path[path_len];
    strncpy(tokenized_path, path, path_len);
    
    char *token = strtok(tokenized_path, DIR_DELIMITER);
    
    // Current Directory Entry in path
    struct ext2_dir_entry *curr_dir_entry = find_entry(curr_inode,
                                                       CURRENT_DIR);
    
    while (token != NULL) {
        curr_inode =  i_table + INDEX(curr_dir_entry->inode);
        curr_dir_entry = find_entry(curr_inode, token);
        
        char *next_token = strtok(NULL, DIR_DELIMITER);
        
        // One or more entries in the path don't exist
        if (curr_dir_entry == NULL) {
            exit(ENOENT);
        }
        
        // Reached end of path. Target directory found.
        else if (next_token == NULL) {
            if (!path_terminator_valid(path, curr_dir_entry)) {
                exit(ENOENT);
            }
            return curr_dir_entry;
        }
        
        // One or more dir entries in path is a sym link
        else if (curr_dir_entry->file_type == EXT2_FT_SYMLINK) {
            // As clarified by instructor, we are not required to handle this
            exit(ENOENT);
        }
        
        // One or more entries in the path is not a sym link or a directory
        else if (curr_dir_entry->file_type != EXT2_FT_DIR) {
            exit(ENOENT);
        }
        
        // Everything good, continue traversing path
        token = next_token;
    }
    
    // Make sure there is no trailing '/' at the end of 'path', if 'path'
    // does not refer to a directory
    if (!path_terminator_valid(path, curr_dir_entry)) {
        exit(ENOENT);
    }
    
    return curr_dir_entry;
}


/*
 * Creates a file with the given `name` at the directory referred by the
 * given `path`.
 *
 * Returns the directory entry for the newly created file.
 */
struct ext2_dir_entry *create_target_file(char *path, char *name,
                                          unsigned int link_inode,
                                          unsigned char file_type) {
    
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
        
        // Curr entry is a 'Sym link'
        if (curr_dir_entry->file_type == EXT2_FT_SYMLINK) {
            
            // One (or more) of the entries in the path is not a 'Directory'
            if (strlen(rem_path) != 0) {
                exit(ENOENT);
            }
            else {
                // A link already exists by this name
                exit(EEXIST);
            }
        }
        
        // One or more entries in the path is not a sym link or a directory
        else if (curr_dir_entry->file_type != EXT2_FT_DIR) {
            if (strlen(rem_path) != 0) {
                exit(ENOENT);
            }
            
            // A non-directory target already exists
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
    
    return create_dir_entry(curr_inode, link_inode, name, file_type);
}


/*
 * Copies `path` into the first block of the inode and updates the necessary
 * fields in the inode.
 */
void copy_symlink_path(struct ext2_dir_entry *dir_entry, char *path) {
    
    struct ext2_inode *inode = get_inode_table() + INDEX(dir_entry->inode);
    
    // The length of the path, which is also the size of the sym link
    unsigned int path_len = strlen(path);
    
    // Allocate block and store absolute path to link
    int block_num = allocate_block();
    unsigned char* block = BLOCK_START(disk, block_num);
    memcpy(block, path, path_len);
    
    // Update inode and make it point to the block containing the path
    inode->i_block[0] = block_num;
    inode->i_size = path_len;
    inode->i_blocks = NUM_DISK_BLKS(inode->i_blocks, EXT2_BLOCK_SIZE);
}

/*
 * Creates a link from at `link_path` to `src_path`.
 *
 * Returns EXIT_SUCCESS, if the link was successfully created.
 *               ENOENT, if either of the paths are invalid or don't exist
 *               EISDIR, if the src is a direcotry and the link is 'hard'
 *               EEXIST, if the target already contains a file by same name
 */
int create_link(char *src_path, char *link_path, unsigned char link_type) {
    
    struct ext2_dir_entry *src_dir_entry = find_dir_entry(src_path);
    
    if (src_dir_entry == NULL) {
        return ENOENT;
    }
    
    // Don't allow hard links to directories
    if (src_dir_entry->file_type == EXT2_FT_DIR &&
        link_type != EXT2_FT_SYMLINK) {
        
        return EISDIR;
    }
    
    // Use src file name if no name has been provided for target
    unsigned int src_file_name_len = src_dir_entry->name_len + 1;
    char src_file_name[src_file_name_len];
    
    strncpy(src_file_name, src_dir_entry->name, src_file_name_len);
    src_file_name[src_file_name_len - 1] = '\0';
    
    struct ext2_dir_entry *link;
    
    // Symbolic link
    if (link_type == EXT2_FT_SYMLINK) {
        link = create_target_file(link_path, src_file_name,
                                  UNDEFINED, link_type);
        
        copy_symlink_path(link, src_path);
    }
    
    // Hard link
    else {
        link = create_target_file(link_path, src_file_name,
                                  src_dir_entry->inode, link_type);
    }
    
    return EXIT_SUCCESS;
}


int main(int argc, char *argv[]) {
    
    if (argc < MIN_ARGUMENT_V || argc > MAX_ARGUMENT_V) {
        fprintf(stderr, USAGE, argv[0]);
        return EXIT_FAILURE;
    }
    
    unsigned char target_file_type = EXT2_FT_REG_FILE;
    char *disk_image_path = argv[1];
    char *source_path;
    char *link_path;
    
    // Check if flag for symoblic link is present
    if (argc == MAX_ARGUMENT_V) {
        char *flag = argv[2];
        
        if (flag[1] != SYM_LINK_FLAG) {
            fprintf(stderr, USAGE, argv[0]);
            return EXIT_FAILURE;
        }
        target_file_type = EXT2_FT_SYMLINK;
        source_path = argv[3];
        link_path = argv[4];
    }
    else {
        source_path = argv[2];
        link_path = argv[3];
    }
    
    disk = read_disk_image(disk_image_path);

    return create_link(source_path, link_path, target_file_type);
    
}