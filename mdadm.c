/* Author: Ziyu Lin
   Date: Apr.23, 2024
    */


#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "cache.h"
#include "mdadm.h"
#include "util.h"
#include "jbod.h"
#include "net.h"

// Keep track of mount status of JBOD system
// 0-unmounted, 1-mounted
static int is_mounted = 0;

int mdadm_mount(void) {
    int result;
    // Check if the system is already mounted, if yes, which means there have been commands, then failed
    if (is_mounted == 1) {
        return -1;
    }
    // Mount the JBOD system
    result = jbod_client_operation(JBOD_MOUNT << 14, NULL);  // Shift left to point to Command in 14-19 bits to perform operation. Block can be NULL provided by instruction
    // Check if the JBOD mount operation was successful
    // 0-success, -1-failed, as per JBOD system
    if (result == 0) {
        is_mounted = 1;  // Indicate mounted
        return 1;
    } else {
        return -1;
    }
}

int mdadm_unmount(void) {
    int result;
    // Check if the system is already unmounted, if yes, then failed
    if (is_mounted != 1) {
        return -1;
    }
    // Unmount the JBOD system
    result = jbod_client_operation(JBOD_UNMOUNT << 14, NULL);  // Shift left to point to Command. Block can be NULL
    // Check if the JBOD unmount operation was successful
    // 0-success, -1-failed, as per JBOD system
    if (result == 0) {
        is_mounted = 0;  // Indicate unmounted
        return 1;
    } else {
        return -1;
    }
}

// Helper function to calculate disk and block IDs
static void disk_block_id(uint32_t addr, uint32_t *disk_id, uint32_t *block_id) {
    *disk_id = addr / JBOD_DISK_SIZE;
    *block_id = (addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
    uint32_t address_bound = addr + len;
    // Read should fail on an umounted system, on a NULL pointer but not for 0-length, on larger than 1024-byte I/O sizes, on an out-of-bound linear address
    if (is_mounted != 1 || (len != 0 && buf == NULL) || len > 1024 || address_bound > JBOD_NUM_DISKS * JBOD_DISK_SIZE) {
        return -1;
    }

    // Tracking the current disk and block ID, the current address, and the number of bytes read
    uint32_t disk_id, block_id;
    uint32_t current_addr = addr;
    uint32_t bytes_read = 0;

    // Dynamically allocate the temp_buf for block read
    uint8_t *temp_buf = (uint8_t *)malloc(JBOD_BLOCK_SIZE);
    if (temp_buf == NULL) {
        // If malloc fails, return error
        return -1;
    }

    // Read until the current address reaches the address bound
    while (current_addr < address_bound) {
        disk_block_id(current_addr, &disk_id, &block_id);

        // Calculate the data starts at what bytes into the block
        // If offset is 0, current_addr aligns with the start of a block, otherwise falls within a block
        uint32_t block_offset = current_addr % JBOD_BLOCK_SIZE;
        // Find how many bytes remain in the current block from offset
        uint32_t bytes_in_block = JBOD_BLOCK_SIZE - block_offset;
        // Calculate the number of bytes to read in this iteration
        // Remaining bytes should not exceed the bound, or it will cause stack overflow
        uint32_t read_now;
        if (bytes_in_block < (address_bound - current_addr)) {
            read_now = bytes_in_block;
        } else {
            read_now = address_bound - current_addr;
        }

        // Check if the cache is enabled and the block is in the cache
        if (cache_enabled() && cache_lookup(disk_id, block_id, temp_buf) == 1) {
            // If the block is in the cache, copy the block data into the buffer
            memcpy(buf + bytes_read, temp_buf + block_offset, read_now);
        } else {
            // If the block is not in the cache, read the block from the disk
            // Seek to current disk
            jbod_client_operation((JBOD_SEEK_TO_DISK << 14) | (disk_id << 28), NULL);
            // Seek to current block of the disk
            jbod_client_operation((JBOD_SEEK_TO_BLOCK << 14) | (block_id << 20), NULL);
            // Read the block from the disk
            int read_block = jbod_client_operation(JBOD_READ_BLOCK << 14, temp_buf);
            // Check if read block operation failed
            if (read_block != 0) {
                // Free temp_buf on failure
                free(temp_buf);
                return -1;
            }

            if (cache_enabled()) {
                // Insert the block into the cache
                cache_insert(disk_id, block_id, temp_buf);
            }
            // Copy temp_buf into buffer
            memcpy(buf + bytes_read, temp_buf + block_offset, read_now);
        }

        // Update the total number of bytes read and current address for next iteration
        bytes_read = bytes_read + read_now;
        current_addr = current_addr + read_now;
    }

    // Free temp_buf after use
    free(temp_buf);
    // Get the total number of bytes read
    return bytes_read;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
    uint32_t address_bound = addr + len;
    // Write should fail on an unmounted system, on a NULL pointer but not for 0-length, on larger than 1024-byte I/O sizes, on an out-of-bound linear address
    if (is_mounted != 1 || (len != 0 && buf == NULL) || len > 1024 || address_bound > JBOD_NUM_DISKS * JBOD_DISK_SIZE) {
        return -1;
    }

    // Tracking the current disk and block ID, the current address, the number of bytes written, and the number of bytes remaining
    uint32_t disk_id, block_id;
    uint32_t current_addr = addr;
    uint32_t bytes_written = 0;
    uint32_t bytes_remaining = len;

    // Dynamically allocate the temp_buf for block read
    uint8_t *temp_buf = (uint8_t *)malloc(JBOD_BLOCK_SIZE);
    if (temp_buf == NULL) {
        // If malloc fails, return error
        return -1;
    }

    // Write until the remaining bytes reaches to 0
    while (bytes_remaining > 0) {
        disk_block_id(current_addr, &disk_id, &block_id);
        // Calculate the data starts at what bytes into the block
        // If offset is 0, current_addr aligns with the start of a block, otherwise falls within a block
        uint32_t block_offset = current_addr % JBOD_BLOCK_SIZE;
        // Find how many bytes remain in the current block from offset
        uint32_t bytes_in_block = JBOD_BLOCK_SIZE - block_offset;
        // Calculate the number of bytes to write in this iteration
        // Remaining bytes should not exceed the bound, or it will cause stack buffer overflow
        uint32_t write_now;
        if (bytes_remaining < bytes_in_block) {
            write_now = bytes_remaining;
        } else {
            write_now = bytes_in_block;
        }

        // Track the cache hit status
        int cache_hit = -1;
        // Check if the cache is enabled
        if (cache_enabled()) {
            // Cache operation to update the cache hit status
            cache_hit = cache_lookup(disk_id, block_id, temp_buf);
        }
        // Check if the cache hit status is 1, which means the block is in the cache
        if (cache_hit != 1) {
            // Seek to the disk and block
            jbod_client_operation((JBOD_SEEK_TO_DISK << 14) | (disk_id << 28), NULL);
            jbod_client_operation((JBOD_SEEK_TO_BLOCK << 14) | (block_id << 20), NULL);
            // If writing part of a block, read the current block, modify it, and write it back.
            int read_block = jbod_client_operation(JBOD_READ_BLOCK << 14, temp_buf);
            // Check if read block operation failed
            if (read_block != 0) {
                // Free temp_buf on failure
                free(temp_buf);
                return -1;
            }
        }

        // Copy the data to be written into the temp_buf
        memcpy(temp_buf + block_offset, buf + bytes_written, write_now);

        // Seek again to the correct position and write the block
        jbod_client_operation((JBOD_SEEK_TO_DISK << 14) | (disk_id << 28), NULL);
        jbod_client_operation((JBOD_SEEK_TO_BLOCK << 14) | (block_id << 20), NULL);
        int write_block = jbod_client_operation(JBOD_WRITE_BLOCK << 14, temp_buf);
        // Check if write block operation failed
        if (write_block != 0) {
            // Return -1 if the write operation failed
            // Free temp_buf on failure
            free(temp_buf);
            return -1;
        }

        // Check if the cache is enabled
        if (cache_enabled()) {
            // Check if the block is in the cache
            if (cache_hit == 1) {
                // Update the cache if the block is in the cache
                cache_update(disk_id, block_id, temp_buf);
            } else {
                // Insert the block into the cache if it is not in the cache
                cache_insert(disk_id, block_id, temp_buf);
            }
        }

        // Update the total number of bytes written, current address, and remaining bytes for the next iteration
        bytes_written += write_now;
        bytes_remaining -= write_now;
        current_addr += write_now;
    }

    // Free temp_buf after use
    free(temp_buf);
    // Get the total number of bytes written
    return bytes_written;
}
