# mdadm-Linear-Device
  Multiple Disk and Device Administration

**Developed on Linux**

**Please replace the jbod.o and jbod_server if using on ARM Architecture based devices** 

Provide disks as a JBOD (Just a Bunch of Disks), which is a storage architecture consisting of numerous disks inside of a single storage enclosure. 

Bits  Width Field    Description

28-31  4    DiskID   This is the ID of the disk to perform operation on

20-27  8    BlockID  Block address within the disk

14-19  6    Command  This is the command to be executed by JBOD.

0-13  14    Reserved Unused bits (for now)

Each of the disks consists of 256 blocks, and each block has 256 bytes, coming to a total of 256 × 256 = 65,536 bytes per disk. The combined capacity is 16 × 65,536 = 1,048,576 bytes = 1 MB. 

The device driver with a single function to control the disks.

* int jbod_operation(uint32_t op, uint8_t *block); This function returns 0 on success and -1 on failure. It accepts an operation through the op parameter, the format of which is described previously, and a pointer to a buffer. The command field can be one of the following commands, which are declared as a C enum type in the header:

* JBOD_MOUNT: mount all disks in the JBOD and make them ready to serve commands. This is the first command that should be called on the JBOD before issuing any other commands; all commands before it will fail. When the command field of op is set to this command, all other fields in op are ignored by the JBOD driver. Similarly, the block argument passed to jbod_operation can be NULL.

* JBOD_UNMOUNT: unmount all disks in the JBOD. This is the last command that should be called on
the JBOD; all commands after it will fail. When the command field of op is set to this command, all other fields in op are ignored by the JBOD driver. Similarly, the block argument passed to jbod_operation can be NULL.

* JBOD_SEEK_TO_DISK: seeks to a specific disk. JBOD internally maintains an I/O position, a tuple consisting of {CurrentDiskID, CurrentBlockID}, which determines where the next I/O operation will happen. This command seeks to the beginning of disk specified by DiskID field in op. In other words, it modifies I/O position: it sets CurrentDiskID to DiskID specified in op and it sets CurrentBlockID to 0. When the command field of op is set to this command, the BlockID field in op is ignored by the JBOD driver. Similarly, the block argument passed to jbod_operation can be NULL.

* JBOD_SEEK_TO_BLOCK: seeks to a specific block in current disk. This command sets the CurrentBlockID in I/O position to the block specified in BlockID field in op. When the command field of op is set to this command, the DiskID field in op is ignored by the JBOD driver. Similarly, the block argument passed to jbod_operation can be NULL.

* JBOD_READ_BLOCK: reads the block in current I/O position into the buffer specified by the block argument to jbod_operation. The buffer pointed by block must be of block size, that is 256 bytes. After this operation completes, the CurrentBlockID in I/O position is incremented by 1; that is, the next I/O operation will happen on the next block of the current disk unless you specify a new DiskID or BlockID. When the command field of op is set to this command, all other fields in op are ignored by the JBOD driver.

* JBOD_WRITE_BLOCK: writes the data in the block buffer into the block in the current I/O position. The buffer pointed by block must be of block size, that is 256 bytes. After this operation completes, the CurrentBlockID in I/O position is incremented by 1; that is, the next I/O operation will happen on the next block of the current disk unless you specify a new DiskID or BlockID. When the command field of op is set to this command, all other fields in op are ignored by the JBOD driver.


Mdadm stands for multiple disk and device administration, and it is a tool for doing cool tricks with multiple disks. 

A linear device makes multiple disks appear as a one large disk to the operating system. 

In our case, we will configure 16 disks of size 64 KB as a single 1 MB disk and will merge these 16 disks into a single logical disk with a linear address space. 

This linear address space allows for reading and writing bytes within the range of 0 to 1,048,575. To clarify, the mdadm linear device should map this linear address space to the disks sequentially. For instance, addresses 0 to 65,535 correspond to disk 0, addresses 65,536 to
131,071 correspond to disk 1, and so forth.

**Below are the main functions of mdadm:**

* int mdadm_mount(void): Mount the linear device; now mdadm user can run read and operations on the linear address space that combines all disks. It should return 1 on success and -1 on failure. Calling this function the second time without calling mdadm_unmount in between, should fail.

* int mdadm_unmount(void): Unmount the linear device; now all commands to the linear device should fail. It should return 1 on success and -1 on failure. Calling this function the second time without calling mdadm_mount in between, should fail.

* int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf): Read len bytes into buf starting at addr. It returns -1 on failure and actual length of read data in case of success. Read from an out-of-bound linear address should fail. A read larger than 1,024 bytes should fail; in other words, len can be 1,024 at most. 

* int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf): Write len bytes from the user-supplied buf buffer to storage system, starting at address addr. The buf parameter has a const specifier. We put the const there to emphasize that it is an in parameter; that is, mdadm_write should only read from this parameter and not modify it. Similar to mdadm_read, writing to an out-of-bound linear address should fail. A read larger than 1,024 bytes should fail; in other words, len can be 1,024 at most. 

A cache is an integral part of a storage system and it is not accessible to the users of the storage system. We implement cache as a separate module, and then integrate it to mdadm_read and mdadm_write calls.


**Below are the main functions of cache:**

* int cache_create(int num_entries); Dynamically allocate space for num_entries cache
entries and should store the address of the created cache in the cache global variable. The num_entries argument can be 2 at minimum and 4096 at maximum. It should also set cache_size
to num_entries, since that describes the size of the cache and will also be used by other functions. cache_size is fixed once the cache is created. You can view it as the maximum capacity of the cache. As such, for simplicity you’d implement it as an array of size cache_size instead of a linked list, although the latter allows one to dynamically adding or deleting cache entries. Calling this function twice without an intervening cache_destroy call (see below) should fail.

* int cache_destroy(void); Free the dynamically allocated space for cache, and should set cache
to NULL, and cache_size to zero. Calling this function twice without an intervening cache_-
create call should fail.

* int cache_lookup(int disk_num, int block_num, uint8_t *buf); Lookup the block
identified by disk_num and block_num in the cache. If found, copy the block into buf, which cannot be NULL. This function must increment num_queries global variable every time it performs a lookup. If the lookup is successful, this function should also increment num_hits global variable; it should also increment clock variable and assign it to the access_time field of the corresponding entry, to indicate that the entry was used recently. We are going to use num_queries and num_hits variables to compute your cache’s hit ratio.

* int cache_insert(int disk_num, int block_num, uint8_t *buf); Insert the block identified by disk_num and block_num into the cache and copy buf—which cannot be NULL—to the corresponding cache entry. Insertion should never fail: if the cache is full, then an entry should be overwritten according to the LRU policy using data from this insert operation. This function should also increment and assign clock variable to the access_time of the newly inserted entry.

* void cache_update(int disk_num, int block_num, const uint8_t *buf); If the entry identified by disk_num and block_num exists in cache, updates its block content with the new data in buf. Should also update the access_time if successful. This function may be called when you perform “write”.

* bool cache_enabled(void); Returns true if cache is enabled (cache_size is larger than the minimum 2). This will be useful when integrating the cache to your mdadm_read and mdadm_write functions. That is, in mdadm functions, we call this function first whenever cache is possibly involved.

Finally, we implemented a client component of the protocol that will connect to the JBOD server and execute JBOD operations over the network. As the company scales, they plan to add multiple JBOD systems to their data center. Having networking support in mdadm will allow the company to avoid downtime in case a JBOD system malfunctions, by switching to another JBOD system on the fly.

**[Here](https://github.com/zbl5332/mdadm-Local) is the repository of No Network Ability mdadm-Linear-Device**

**The mdadm code was replace all calls to jbod_operation with jbod_client_operation, which will send JBOD commands over a network to a JBOD server that can be anywhere on the Internet (but will most probably be in the data center of the company).**


The protocol defined by the JBOD vendor has two messages. The JBOD request message is sent from client program to the JBOD server and contains an opcode and a buffer when needed (e.g., when your client needs to write a block of data to the server side jbod system). The JBOD response message is sent from the JBOD server to client program and contains an opcode and a buffer when needed (e.g., when your client needs to read a block of data from the server side jbod system). 

Both messages use the same format:

Bytes Field       Description

0-1   length      The size of the packet in bytes

2-5   opcode      The opcode for the JBOD operation

6-7   return code Return code from the JBOD operation (i.e., returns 0 on success and -1 on failure)

8-263 block       Where needed, a block of size JBOD BLOCK SIZE

**In a nutshell, there are four steps for network:**

* The client side (inside the function jbod_client_operation) wraps all the parameters of a jbod
operation into a JBOD request message and sends it as a packet to the server side

* The server receives the request message, extract the relevant fields (e.g., opcode, block if needed), issues the jbod_operation function to its local jbod system and receives the return code

* The server wraps the fields such as opcode, return code and block (if needed) into a JBOD response message and sends it to the client

* The client (inside the function jbod_client_operation) next receives the response message, extracts the relevant fields from it, and returns the return code and fill the parameter “block” if needed.

Note that the first three fields (i.e., length, opcode and return code) of JBOD protocol messages can be considered as packet header, with the size HEADER LEN predefined in net.h. The block field can be considered is the optional payload. 

**Trace files are provided for Performance and Hit Rate Check**


