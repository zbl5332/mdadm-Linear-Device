#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
    int total_read = 0;
    int bytes_read;

    // Keep reading until the total number of bytes read is equal to the length
    while (total_read < len) {
        // Read from the file descriptor
        bytes_read = read(fd, buf + total_read, len - total_read);
        // Check if the read operation failed
        if (bytes_read <= 0) {
            return false;
        }
        total_read += bytes_read;
    }
    return true;
}


/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
    int total_written = 0;
    int bytes_written;

    // Keep writing until the total number of bytes written is equal to the length
    while (total_written < len) {
        // Write to the file descriptor
        bytes_written = write(fd, buf + total_written, len - total_written);
        // Check if the write operation failed
        if (bytes_written <= 0) {
            return false;
        }
        total_written += bytes_written;
    }
    return true;
}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {
    uint8_t packet[HEADER_LEN];

    // Check if the packet header can be read
    if (nread(sd, HEADER_LEN, packet) == false) {
        return false;
    }

    // Extract the packet length, opcode, and return value
    int packet_len = ntohs(*(uint16_t *)packet);
    *op = ntohl(*(uint32_t *)(packet + 2));
    *ret = ntohs(*(uint16_t *)(packet + 6));

    // Check if the packet length is greater than the header length and the block is not NULL
    if (packet_len > HEADER_LEN && block != NULL) {
        // Read the block data from the server
        if (nread(sd, JBOD_BLOCK_SIZE, block) == true) {
            return true;
        }
    }
    return true;
}


/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
    uint8_t packet[HEADER_LEN + JBOD_BLOCK_SIZE];
    // Create the packet with the opcode and block data
    int packet_len = HEADER_LEN + ((block != NULL) ? JBOD_BLOCK_SIZE : 0);
    *(uint16_t *)packet = htons(packet_len);
    *(uint32_t *)(packet + 2) = htonl(op);

    // Check if the block is not NULL
    if (block != NULL) {
        // Copy the block data into the packet
        memcpy(packet + HEADER_LEN, block, JBOD_BLOCK_SIZE);
    }

    // Write the packet to the server
    if (nwrite(sd, packet_len, packet) == false){
        return false;

    }
    return true;
}


/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
    struct sockaddr_in server_addr;
    int sock;

    // Check if the socket can be created
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        return false;
    }

    // Set the server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Check if the IP address can be converted
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        close(sock);
        return false;
    }

    // Check if the socket can be connected
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(sock);
        return false;
    }

    cli_sd = sock;
    return true;
}


/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
    if (cli_sd != -1) {
        close(cli_sd);
        cli_sd = -1;
    }
}


/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
    uint32_t opcode = op >> 14;
    bool write = opcode == JBOD_WRITE_BLOCK;

    // Send a packet to the server
    uint8_t *send_data = NULL;
    if (write == true) {
        send_data = block;
    } else {
        send_data = NULL;
    }
    // Check if the packet can be sent
    if (send_packet(cli_sd, op, send_data) == false) {
        return -1;
    }

    uint16_t return_code;

    // Receive a packet from the server
    uint8_t *receive_target = NULL;
    if (write == true) {
        receive_target = NULL;
    } else {
        receive_target = block;
    }

    // Check if the packet can be received
    if (recv_packet(cli_sd, &opcode, &return_code, receive_target) == false) {
        return -1;
    }

    return (int)return_code;
}
