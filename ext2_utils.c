#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>

#include "ext2_utils.h"

extern unsigned char *disk;

/*
 * Opens the virtual disk image at the given path
 */
unsigned char *read_disk_image(char *path) {
    
    int fd = open(path, O_RDWR);

    unsigned char *disk = mmap(NULL, NUM_BLOCKS * EXT2_BLOCK_SIZE,
                               PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    if (disk == MAP_FAILED) {
        perror("mmap - Could not open disk image");
        exit(EXIT_FAILURE);
    }
    
    return disk;
}

struct ext2_super_block *get_super_block() {
    return (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
}

struct ext2_group_desc *get_group_descriptor() {
    unsigned int group_desc_block_num = 2;
    
    return (struct ext2_group_desc *) BLOCK_START(disk, group_desc_block_num);
}

struct ext2_inode *get_inode_table() {
    struct ext2_group_desc *gd = get_group_descriptor();
      
    return (struct ext2_inode *) BLOCK_START(disk, gd->bg_inode_table);
}

unsigned char *get_block_bitmap() {
    struct ext2_group_desc *gd = get_group_descriptor();

    return BLOCK_START(disk, gd->bg_block_bitmap);
}

unsigned char *get_inode_bitmap() {
    struct ext2_group_desc *gd = get_group_descriptor();
    
    return BLOCK_START(disk, gd->bg_inode_bitmap);
}

int get_blocks_count() {
    struct ext2_super_block *sb = get_super_block();
    return sb->s_blocks_count;
}

int get_inodes_count() {
    struct ext2_super_block *sb = get_super_block();
    return sb->s_inodes_count;
}


unsigned int get_timestamp() {
    unsigned int current_time = (unsigned int) time(NULL);
    
    if (((time_t) current_time) == ((time_t) -1)) {
        exit(EXIT_FAILURE);
    }
    
    return current_time;
}


/*
 * Sets the first low bit in the bitmap to high, and returns
 * number of the resource (i.e. inode or block) corresponding
 * to that bit.
 */
int allocate_resource(unsigned char *bitmap, int bitmap_size) {
    int i;
    for (i = 0; i < bitmap_size; i++) {
        unsigned char curr_byte = bitmap[i];
        
        int curr_bit;
        for (curr_bit = 0; curr_bit < CHAR_BIT; curr_bit++) {
            
            if (!IS_IN_USE(curr_byte, curr_bit)) {
                
                // Reserve new block / inode
                bitmap[i] |= 1 << curr_bit;
                
                // Return the number of the newly reserved block / inode
                return NUM((i * CHAR_BIT) + curr_bit);
            }
        }
    }
    
    // Should only reach here if there are no free resources available
    exit(ENOMEM);
}


/*
 * Returns 1 if the bit for the corrsponding resource (i.e. inode or block)
 * is set to high.
 * Returns 0, otherwise.
 */
int is_resource_in_use(unsigned char *bitmap, int resource_num) {
    int index = INDEX(resource_num);
    
    int byte_index = index / CHAR_BIT;
    int bit_offset = index % CHAR_BIT;
    
    char byte = bitmap[byte_index];
    
    return IS_IN_USE(byte, bit_offset);
}


int is_inode_in_use(unsigned int inode_num) {
    unsigned char *inode_bitmap = get_inode_bitmap();
    
    return is_resource_in_use(inode_bitmap, inode_num);
}

int is_block_in_use(unsigned int block_num) {
    unsigned char *block_bitmap = get_block_bitmap();
    
    return is_resource_in_use(block_bitmap, block_num);
}


/*
 * Sets the corresponding bit for the given resource_num to high.
 */
void set_resource_in_use(unsigned char *bitmap, int resource_num) {
    int index = INDEX(resource_num);
    
    int byte_index = index / CHAR_BIT;
    int bit_offset = index % CHAR_BIT;
    
    // Set the appropriate bit in bitmap to 1
    bitmap[byte_index] |= 1 << bit_offset; 
}


/*
 * Sets the corresponding bit for the given resource_num to low.
 */
void free_resource(unsigned char *bitmap, int resource_num) {
    int index = INDEX(resource_num);
    
    int byte_index = index / CHAR_BIT;
    int bit_offset = index % CHAR_BIT;
    
    // Set the appropriate bit in bitmap to 0
    bitmap[byte_index] &= (~(1 << bit_offset));
}

/*
 * Returns the corresponding i_mode value base on the given directory entry
 * file_type.
 */
unsigned short get_inode_mode(unsigned char file_type) {
    switch (file_type) {
        case EXT2_FT_DIR:
            return EXT2_S_IFDIR;
        case EXT2_FT_REG_FILE:
            return EXT2_S_IFREG;
        default:
            return EXT2_S_IFLNK;
    }
}


/*
 * Allocates a new inode for the given file_type.
 *
 * Returns the number of the allocated inode.
 */
int allocate_inode(unsigned char file_type) {
    unsigned char *inode_bitmap = get_inode_bitmap();
    int bitmap_size = get_inodes_count();
    
    int inode_num = allocate_resource(inode_bitmap, bitmap_size);
    get_group_descriptor()->bg_free_inodes_count--;
    get_super_block()->s_free_inodes_count--;
    
    // Zero-out the allocated inode
    struct ext2_inode *inode = get_inode_table() + INDEX(inode_num);
    memset(inode, 0, sizeof(struct ext2_inode));
    
    unsigned int current_time = get_timestamp();
    
    // Init inode values
    inode->i_mode |= get_inode_mode(file_type);
    inode->i_ctime = current_time;
    inode->i_atime = current_time;
    inode->i_mtime = current_time;
    
    return inode_num;
}

/*
 * Marks the given inode as free, and updates the appropriate counters.
 */
void free_inode(unsigned int inode_num) {
    unsigned char *inode_bitmap = get_inode_bitmap();
    
    free_resource(inode_bitmap, inode_num);
    get_group_descriptor()->bg_free_inodes_count++;
    get_super_block()->s_free_inodes_count++;
}


/*
 * Marks the given block as free, and updates the appropriate counters.
 */
void free_block(unsigned int block_num) {
    unsigned char *block_bitmap = get_block_bitmap();
    
    free_resource(block_bitmap, block_num);
    get_group_descriptor()->bg_free_blocks_count++;
    get_super_block()->s_free_blocks_count++;
}


/*
 * Frees all data blocks pointed by the given inode.
 */
void free_data_blocks(struct ext2_inode *inode) {
    int n;
    
    // Free direct blocks
    for (n = 0; n < NUM_DIRECT_PTRS && inode->i_block[n] != 0; n++) {
        unsigned int block_num = inode->i_block[n];
        free_block(block_num);
    }
    
    unsigned int indirect_block_num = inode->i_block[n];
    
    // Free indirect blocks if there are any
    if (indirect_block_num != 0) {
        
        int max_indirect_blocks = EXT2_BLOCK_SIZE / sizeof(unsigned int);
        int blocks_freed = 0;
        
        // Position within indirect block
        unsigned int *pos = (unsigned int *)
                                        BLOCK_START(disk, indirect_block_num);

        // Free 2nd level blocks pointed by the indirect block
        unsigned int direct_block_num = *(pos++);
        
        while (blocks_freed < max_indirect_blocks && direct_block_num != 0) {
            free_block(direct_block_num);
            direct_block_num = *(pos++);
            blocks_freed++;
        }
        
        // Free the indirect block iteself
        free_block(indirect_block_num);
    }
}


/*
 * Decrements the links_count of the given inode by 1.
 *
 * Marks the inode as 'free' if its links_count reaches 0.
 */
void unlink_inode(unsigned int inode_num) {
    
    struct ext2_inode *inode = get_inode_table() + INDEX(inode_num);
    
    if (inode->i_links_count == 0) {
        // Trying to unlink an inode that already has no links
        exit(EXIT_FAILURE);
    }
    
    inode->i_links_count--;
    
    // Free (delete) the inode and its data blocks if it has no more links
    if (inode->i_links_count == 0) {
        
        inode->i_dtime = get_timestamp();
        free_data_blocks(inode);
        free_inode(inode_num);
    }
}

/*
 * Marks the given inode as in-use, and updates the appropriate counters.
 */
void set_inode_in_use(unsigned int inode_num) {
    
    if (is_inode_in_use(inode_num)) {
        // Should not reach here, since the caller should do this check
        // before trying to re-claim the inode
        exit(EXIT_FAILURE);
    }
    
    unsigned char *inode_bitmap = get_inode_bitmap();
    
    set_resource_in_use(inode_bitmap, inode_num);
    
    get_group_descriptor()->bg_free_inodes_count--;
    get_super_block()->s_free_inodes_count--;
}


/*
 * Marks the given block as in-use, and updates the appropriate counters.
 */
void set_block_in_use(unsigned int block_num) {
    
    if (is_block_in_use(block_num)) {
        // Should not reach here, since the caller should do this check
        // before trying to re-claim the block
        exit(EXIT_FAILURE);
    }
    
    unsigned char *block_bitmap = get_block_bitmap();
    
    set_resource_in_use(block_bitmap, block_num);
    
    get_group_descriptor()->bg_free_blocks_count--;
    get_super_block()->s_free_blocks_count--;
}


/*
 * Allocates a new block and returns its number.
 */
int allocate_block() {
    unsigned char *block_bitmap = get_block_bitmap();
    int bitmap_size = get_blocks_count();
    
    int block_num = allocate_resource(block_bitmap, bitmap_size);
    get_group_descriptor()->bg_free_blocks_count--;
    get_super_block()->s_free_blocks_count--;
    
    // Zero-out the newly allocated block
    unsigned char *block = disk + (block_num * EXT2_BLOCK_SIZE);
    memset(block, 0, EXT2_BLOCK_SIZE);
    
    return block_num;
}


/*
 * Returns rec_len rounded up to the next multiple of 4.
 */
int get_padded_rec_len(int rec_len) {

    if (rec_len % DIR_ENTRY_ALIGNMENT != 0) {
        int factor = ((rec_len / DIR_ENTRY_ALIGNMENT) + 1);
        rec_len = DIR_ENTRY_ALIGNMENT * factor;
    }
    return rec_len;
}


/*
 * Returns size of the given entry rounded up to the next multiple of 4.
 */
int get_actual_dir_entry_len(struct ext2_dir_entry *entry) {
    int dir_entry_size = sizeof(struct ext2_dir_entry);
    
    return get_padded_rec_len(dir_entry_size + (int) entry->name_len);
}


/*
 * Returns the length of the given file name.
 *
 * Exits the program with appropriate error code if the name length is
 * longer than that allowed by the ext2 file system.
 */
int get_name_len(char *name) {
    int length = strlen(name);
    
    if (length > EXT2_NAME_LEN)
        exit(EXIT_FAILURE);
    
    return length;
}


/*
 * Extracts and returns the file name from the given path.
 */
char *get_file_name(char *path) {
    
    char *token = strtok(path, DIR_DELIMITER);
    char *next_token = token;
    
    while (next_token != NULL) {
        token = next_token;
        next_token = strtok(NULL, DIR_DELIMITER);
    }
    
    return token;
}


/*
 * Initializes a directory entry with the given values.
 */
void init_dir_entry(struct ext2_dir_entry *entry,
                    unsigned int inode, unsigned short rec_len,
                    int name_len, unsigned char file_type, char *name) {
    
    // Create new inode if no existing inode has been provided
    if (inode == UNDEFINED) {
        inode = allocate_inode(file_type);
    }
    
    // Increment links count of inode
    (get_inode_table() + INDEX(inode))->i_links_count++;
    
    entry->inode = inode;
    entry->rec_len = rec_len;
    entry->name_len = name_len;
    entry->file_type = file_type;
    strncpy(entry->name, name, name_len);
    
}


/*
 * Finds and returns the directory entry with the given `name` inside the
 * directory with the given `dir_inode`.
 */
struct ext2_dir_entry *find_entry(struct ext2_inode *dir_inode, char *name) {
    
    if (name == NULL) {
        return NULL;
    }
    
    int name_length = get_name_len(name);
    
    // Iterate over data blocks in search for the matching directory entry
    int n;
    for (n = 0; n < NUM_DIRECT_PTRS && (dir_inode->i_block)[n] != 0; n++) {
        
        int block_num = (dir_inode->i_block)[n];
        
        unsigned char *block_start = disk + (block_num * EXT2_BLOCK_SIZE);
        unsigned char *block_end = BLOCK_END(block_start);
        
        // Current position within this block
        unsigned char *pos = block_start;

        while (pos != block_end) {
            struct ext2_dir_entry *entry = (struct ext2_dir_entry *) pos;
            
            if (entry->inode != 0) { // dir entry is in use
                
                // Check if length of file names match
                if (name_length == ((int) entry->name_len)) {
                    
                    if (strncmp(name, entry->name, name_length) == 0) {
                        // File names match
                        return entry;
                    }
                }
            }
            
            pos += entry->rec_len;
        }
    }
    return NULL;
}


/*
 * Finds and returns the directory entry with the given `name` inside the
 * directory with the given `inode_num`.
 */
struct ext2_dir_entry *find_entry_in_inode(unsigned int inode_num,
                                           char *name) {
    
    struct ext2_inode *inode = get_inode_table() + INDEX(inode_num);
    
    return find_entry(inode, name);
}


/*
 * Creates and returns a directory entry with the given values.
 * The entry is created inside the directory blocks of dir_inode.
 */
struct ext2_dir_entry *create_dir_entry(struct ext2_inode *dir_inode,
                                        unsigned int link_inode,
                                        char *name, unsigned char file_type) {
    
    // Check if an entry by this name already exists
    if (find_entry(dir_inode, name) != NULL) {
        exit(EEXIST);
    }
    
    int name_len = get_name_len(name);
    int dir_entry_size = sizeof(struct ext2_dir_entry);
    int rec_len = get_padded_rec_len(dir_entry_size + name_len);
    
    int n;
    for (n = 0; n < NUM_DIRECT_PTRS && (dir_inode->i_block)[n] != 0; n++) {
        
        int block_num = (dir_inode->i_block)[n];
        
        unsigned char *block_start = disk + (block_num * EXT2_BLOCK_SIZE);
        unsigned char *block_end = BLOCK_END(block_start);
        
        // Current position within this block
        unsigned char *pos = block_start;
        
        while (pos != block_end) {
            struct ext2_dir_entry *entry = (struct ext2_dir_entry *) pos;
            
            if (entry->inode == 0) { // dir entry not in use
                
                // Check if this spot can be reused for the new dir entry
                if (rec_len <= entry->rec_len) {
                    
                    // Insert new entry
                    init_dir_entry(entry, link_inode, entry->rec_len,
                                   name_len, file_type, name);
                    
                    return entry;
                }
            }
            else {
                // The actual length of this dir entry
                int actual_len = get_padded_rec_len(dir_entry_size +
                                                    entry->name_len);
                
                // Check if the new dir entry can be inserted in between
                // two dir entries
                if (rec_len <= (entry->rec_len - actual_len)) {
                    struct ext2_dir_entry *prev = entry;
                    unsigned char *next_entry = (pos + entry->rec_len);
                    
                    // Make previous entry point to the start of the new entry
                    prev->rec_len = actual_len;
                    
                    // Set 'entry' to point to the new entry
                    pos += actual_len;
                    entry = (struct ext2_dir_entry *) pos;
                    
                    // Insert new entry
                    init_dir_entry(entry, link_inode, next_entry - pos,
                                   name_len, file_type, name);
                    
                    return entry;
                }
            }
            
            pos += entry->rec_len;
        }
    }
    
    // Allocate new block if all blocks are full
    if (n < NUM_DIRECT_PTRS && (dir_inode->i_block)[n] == 0) {
        
        int block_num = allocate_block();
        
        // Update inode after allocating new block
        dir_inode->i_block[n] = block_num;
        dir_inode->i_size += EXT2_BLOCK_SIZE;
        dir_inode->i_blocks = NUM_DISK_BLKS(dir_inode->i_blocks, EXT2_BLOCK_SIZE);
        
        unsigned char *block = disk + (block_num * EXT2_BLOCK_SIZE);
        
        struct ext2_dir_entry *entry = (struct ext2_dir_entry *) block;
        int rec_len = BLOCK_END(block) - block;
        
        init_dir_entry(entry, link_inode, rec_len, name_len, file_type, name);
        
        return entry;
    }
    
    // Should only reach here if there are no more free blocks in the disk
    exit(ENOMEM);
}