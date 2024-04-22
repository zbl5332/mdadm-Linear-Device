#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
    // Check if the cache has already created, or the requested number of entries is out of bounds
    if (cache != NULL || num_entries < 2 || num_entries > 4096) {
        return -1;
    }
    // Dynamically allocate memory for the cache entries based on the requested number of entries
    cache = (cache_entry_t *) malloc(num_entries * sizeof(cache_entry_t));
    // Check if the memory allocation failed
    if (cache == NULL) {
        return -1;
    }
    // To track the size of the cache
    cache_size = num_entries;
    // Initialize each cache entry as invalid as empty
    for (int i = 0; i < cache_size; i++) {
        cache[i].valid = false;
    }
    return 1;

}

int cache_destroy(void) {
    // Check if the cache has already been destroyed
    if (cache == NULL) {
        return -1;
    }
    // Free the dynamically allocated memory
    free(cache);
    // Reset the cache pointer to NULL
    cache = NULL;
    return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
    // Check if the cache has not been created
    if (cache == NULL) {
        return -1;
    }
    // Check if the buffer is NULL
    if (buf == NULL) {
        return -1;
    }
    // Increment the number of queries
    ++num_queries;
    // Iterate through the cache entries
    for (int i = 0; i < cache_size; i++) {
        // Check if the cache entry is valid and the disk number and block number match
        if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
            // Increment the number of hits
            ++num_hits;
            // Copy the block data into the buffer
            if (buf != NULL) {
                memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
            }
            // Update the access time of the cache entry
            cache[i].access_time = clock++;
            return 1;
        }
    }
    return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
    if (cache == NULL) {
        return;
    }
    if (buf == NULL) {
        return;
    }
    for (int i = 0; i < cache_size; ++i) {
        // Check if the cache entry is valid and the disk number and block number match
        if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
            // Copy the block data into the cache entry
            memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
            // Update the access time of the cache entry
            cache[i].access_time = clock++;
            return;
        }
    }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
    if (cache == NULL) {
        return -1;
    }
    if (buf == NULL) {
        return -1;
    }
    // Check if the disk number is within bounds
    if (disk_num < 0 || disk_num >= JBOD_NUM_DISKS) {
        return -1;
    }
    // Check if the block number is within bounds
    if (block_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK) {
        return -1;
    }
    for (int i = 0; i < cache_size; i++) {
        // Check if the cache entry is valid and the disk number and block number match
        if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
            return -1;
        }
    }
    // Track the least recently used cache entry
    int lru_index = 0;
    // Initialize the minimum access time to the current clock
    int min_access_time = clock;
    for (int i = 0; i < cache_size; i++) {
        // Check if the cache entry is invalid
        if (!cache[i].valid) {
            // Copy the block data into the cache entry
            cache[i].valid = true;
            cache[i].disk_num = disk_num;
            cache[i].block_num = block_num;
            memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
            // Update the access time of the cache entry
            cache[i].access_time = clock++;
            return 1;
        }
        // Check if the access time is less than the minimum access time
        if (cache[i].access_time < min_access_time) {
            // Update the least recently used cache entry
            lru_index = i;
            // Update the minimum access time
            min_access_time = cache[i].access_time;
        }
    }
    // Evict the least recently used cache entry
    cache[lru_index].valid = true;
    // Update the disk number and block number of the cache entry
    cache[lru_index].disk_num = disk_num;
    cache[lru_index].block_num = block_num;
    // Copy the block data into the cache entry
    memcpy(cache[lru_index].block, buf, JBOD_BLOCK_SIZE);
    // Update the access time of the cache entry
    cache[lru_index].access_time = clock++;
    return 1;
}

bool cache_enabled(void) {
    // Check if the cache has been created with at least 2 entries
    if (cache_size >= 2){
        return true;
    }
    else{
        return false;
    }
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
