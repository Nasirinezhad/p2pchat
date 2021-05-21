#ifndef PEER
#define PEER
#include <netinet/in.h>

typedef struct peer_t {
    char name[32];
    short alive;
    struct sockaddr addr;
} peer;
typedef struct peerList_t {
    char name[32];
    short alive;
    struct sockaddr addr;
    peerList *next;
} peerList;
#endif // !PEER