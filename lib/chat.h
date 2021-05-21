#ifndef CHAT
#include "peer.h"
typedef struct chat_t
{
    char name[32];
    peer *peer_list;
    int num_peers, max_peer;
} chat;
typedef struct chatList_t
{
    char name[32];
    peer *peer_list;
    int num_peers, max_peer;
    chatList *next;
} chatList;

#endif // !CHAT
