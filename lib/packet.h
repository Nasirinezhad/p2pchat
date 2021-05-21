#ifndef PACKET
#define PACKET 
#include <string.h>

#define PAYLOAD_SIZE 1024 
typedef struct message_header_t {
	char type;
	char error;
	unsigned int payload_length;
} message_header;

typedef struct packet_t {
	struct message_header_t header;
	char payload[PAYLOAD_SIZE]; // reserved 4KB
} packet;
#endif // !PACKET