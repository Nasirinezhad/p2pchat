#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <time.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>

#include "chat.h"
#include "packet.h"

chatList *chats;
peerList *all_peers;
int sock, ping_sock;

pthread_mutex_t stdout_lock;
pthread_mutex_t peers_lock;

void sets(int argc, char* argv[]);     //read args and set sock

void announces(struct sockaddr*, packet*);     //peer announces
void reg_chat(struct sockaddr*, packet*);    //register new chat
void join_chat(struct sockaddr*, packet*);     //join a chat
void leave_chat(struct sockaddr*, packet*);    //leave a chat
void chat_list(struct sockaddr*);     //get chat list
void peer_list(struct sockaddr*);     //get peer list
void answer_peer(char, ...);
void chat_alert(char, ...);
int send_packet(packet*, struct sockaddr *);

void * ping_output(void *ptr);
void * ping_input(void *ptr);
void send_pings();
void mark_peer(peerList *, short);

int main(int argc, char* argv[])
{
    sets(argc, argv);

    //create a thread to handle ping responses
    pthread_t ping_input_thread;
    pthread_create(&ping_input_thread, NULL, ping_input, NULL);
    pthread_detach(ping_input_thread);

    //create thread to send pings and delete dead users
    pthread_t ping_output_thread;
    pthread_create(&ping_output_thread, NULL, ping_output, NULL);
    pthread_detach(ping_output_thread);

    socklen_t addrlen = 10;
    struct sockaddr sender_addr;
    packet recv_pkt;
    int recv_status;
    
    while(1)
    {
        recv_status = recvfrom (
                sock,
                &recv_pkt,
                sizeof(recv_pkt),
                0,
                &sender_addr,
                &addrlen);

        if (recv_status == -1) {
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "error - error receiving a packet, ignoring.\n");
            pthread_mutex_unlock(&stdout_lock);
        }else{
            switch (recv_pkt.header.type) {
                case 'a': 
                    announces(&sender_addr, &recv_pkt);
                break;
                case 'r':
                    reg_chat(&sender_addr, &recv_pkt);
                break;
                case 'j':
                    join_chat(&sender_addr, &recv_pkt);
                break;
                case 'l':
                    leave_chat(&sender_addr, &recv_pkt);
                break;
                case 'c':
                    chat_list(&sender_addr);
                break;
                case 'p':
                    peer_list(&sender_addr);
                break;
                default:
                    pthread_mutex_lock(&stdout_lock);
                    fprintf(stderr, "error - received packet type unknown.\n");
                    pthread_mutex_unlock(&stdout_lock);
                break;
            }
        }
    }
    return 1;
}

void sets(int argc, char *argv[])
{
    int port = 8080; //default port
    if(argc > 1){
        port = atoi(argv[1]);
    }
    fprintf(stderr, "Starting server on ports: %d, %d\n", port, port+1);

    //setup UDP sockets
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    ping_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "%s\n", "error - error creating sock.");
        abort();
    }
    if (ping_sock < 0) {
        fprintf(stderr, "%s\n", "error - error creating ping_sock.");
        abort();
    }
    
    struct sockaddr_in self_addr;
    self_addr.sin_family = AF_INET; 
    self_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    self_addr.sin_port = htons(port);

    struct sockaddr_in ping_addr;
    ping_addr.sin_family = AF_INET; 
    ping_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ping_addr.sin_port = htons(port+1);

    if (bind(sock, (struct sockaddr *)&self_addr, sizeof(self_addr))) {
        fprintf(stderr, "%s\n", "error - error binding sock.");
        abort();
    }

    if (bind(ping_sock, (struct sockaddr *)&ping_addr, sizeof(ping_addr))) {
        fprintf(stderr, "%s\n", "error - error binding ping_sock.");
        abort();
    }

    chats = NULL;
    all_peers = NULL;
}

/* Tracking */
int send_packet(packet *pkt, struct sockaddr *addr)
{
    int status = sendto(
        sock,
        &pkt,
        sizeof(pkt),
        0,
        (struct sockaddr *)&addr,
        sizeof(addr)
    );
    if (status == -1) {
        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "error - error sending packet to peer");
        pthread_mutex_unlock(&stdout_lock);
    }

    return status;
}
void announces(struct sockaddr *addr, packet *pkt)
{
    peerList *p;
    if (all_peers == NULL){
        all_peers = malloc(sizeof(peerList));
        p = all_peers;
    }else{
        while (p->next != NULL)
            p = p->next;
        p->next = malloc(sizeof(peerList));
        p = p->next;
    }
    p->addr = *addr;
    p->alive = 1;

    if (sscanf(pkt->payload, "%s", p->name) < 1)
    {
        struct sockaddr_in *addri = (struct sockaddr_in *)addr;
        unsigned int ip = addri->sin_addr.s_addr;
        short port = htons(addri->sin_port);
        sprintf(p->name, "%d:%d", ip, port);
    }
    p->next = NULL;
}
void reg_chat(struct sockaddr *addr, packet *pkt)
{
    char p_name[32], c_name[32];
    int tr, max_peer;
    chatList *p;
    if (tr = sscanf(pkt->payload, "%s %d %s", c_name, max_peer, p_name) > 1)
    {
        if (chats == NULL){
            chats = malloc(sizeof(chatList));
            p = chats;
        }else{
            while (p->next != NULL)
                p = p->next;
            p->next = malloc(sizeof(chatList));
            p = p->next;
        }

        p->max_peer = max_peer;
        strcpy(p->name, c_name);
        p->peer_list = (peer *) malloc(max_peer * sizeof(peer));

        p->peer_list[0].addr = *addr;
        p->peer_list[0].alive = 1;

        if(tr < 3){
            struct sockaddr_in *addri = (struct sockaddr_in *)addr;
            unsigned int ip = addri->sin_addr.s_addr;
            short port = htons(addri->sin_port);
            sprintf(p->peer_list[0].name, "%d:%d", ip, port);
        }else
        {
            strcpy(p->peer_list[0].name, p_name);
        }
        
        p->num_peers = 1;
        p->next = NULL;
    }else
    {
        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "error - Invalid chat args.\n");
        pthread_mutex_unlock(&stdout_lock);
    }
}
void join_chat(struct sockaddr *addr, packet *pkt)
{
    if (chats == NULL){
        // ERR
        return;
    }
    char p_name[32], c_name[32];
    int tr, c_id;
    chatList *p;
    if (tr = sscanf(pkt->payload, "%s %d %s", c_name, c_id, p_name) > 1)
    { 
        p = chats;
        while (p->next != NULL && --c_id > 0)
            p = p->next;

        if (strcmp(p->name, c_name) == 0)
        {
            if (p->num_peers < p->max_peer)
            {
                p->peer_list[p->num_peers].addr = *addr;
                p->peer_list[p->num_peers].alive = 1;

                if(tr < 3){
                    struct sockaddr_in *addri = (struct sockaddr_in *)addr;
                    unsigned int ip = addri->sin_addr.s_addr;
                    short port = htons(addri->sin_port);
                    sprintf(p->peer_list[p->num_peers].name, "%d:%d", ip, port);
                }else
                {
                    strcpy(p->peer_list[p->num_peers].name, p_name);
                }
                answer_peer('c', p);
                chat_alert('j', p);
                p->num_peers++;
            }else
            {
                /* Chat is full */
                return;
            }
        }else
        {
            /* Chat deleted*/
            return;
        }   
    }
}
void leave_chat(struct sockaddr *addr, packet *pkt)
{
    if (chats == NULL){
        // ERR
        return;
    }
    char c_name[32];
    int tr, c_id;
    chatList *p;
    if (tr = sscanf(pkt->payload, "%s %d", c_name, c_id) > 1)
    { 
        p = chats;
        while (p->next != NULL && --c_id > 0)
            p = p->next;

        if (strcmp(p->name, c_name) == 0)
        {
            int i = 0;
            while(i++ < p->num_peers)
            {
                if(memcmp(p->peer_list[i].addr.sa_data, addr->sa_data, 14) == 0
                    &&
                    memcmp(p->peer_list[i].addr.sa_family, addr->sa_family, 14) == 0)
                {
                    p->peer_list[i].alive = 0;
                    break;
                }
            }
            if(p->peer_list[i].alive == 0){
                chat_alert('l', p, i);
                while(++i < p->num_peers)
                    p->peer_list[i-1] = p->peer_list[i];
                p->num_peers--;
            }
        }else
        {
            /* Chat is full */
            return;
        }
    }else
    {
        /* Chat deleted*/
        return;
    }
}
void chat_list(struct sockaddr *addr)
{
    packet pkt;
    pkt.header.type = '1';
    chatList *p = chats;
    FILE *stream;

    stream = open_memstream (&pkt.payload, &pkt.header.payload_length);
    while (p != NULL)// may needs for check memsize  && pkt.header.payload_length < PAYLOAD_SIZE
    {
        if(p->max_peer > p->num_peers)
            fprintf(stream, "%s\n", p->name);
        p = p->next;
    }
    fclose (stream);

    send_packet(&pkt, &addr);
}
void peer_list(struct sockaddr *addr)
{
    packet pkt;
    pkt.header.type = '2';
    peerList *p = all_peers;
    char *saddr = malloc(sizeof(struct sockaddr));
    FILE *stream;

    stream = open_memstream (&pkt.payload, &pkt.header.payload_length);
    while (p != NULL)// may needs for check memsize  && pkt.header.payload_length < PAYLOAD_SIZE
    {
        if(p->alive == 1){
            memcpy(&saddr, &p->addr, sizeof(struct sockaddr));
            fprintf(stream, "%s %s\n", p->name, saddr);
        }
        p = p->next;
    }
    fclose (stream);

    send_packet(&pkt, &addr);
}
void answer_peer(char c, ...)
{
    va_list ap;
    packet pkt;
    struct sockaddr *addr;
    va_start(ap, 1);
    switch (c)
    {
    case 'c':
        {
            pkt.header.type = '3';
            chatList *p = va_arg(ap, chatList *);
            memcpy(&pkt.payload, &p, sizeof(chatList));
            addr = (struct sockaddr *)&p->peer_list[p->num_peers].addr;
        }
        break;
    
    default:
        // Err
        break;
    }
    va_end(ap);
    send_packet(&pkt, addr);
}
void chat_alert(char c, ...)
{
    va_list ap;
    packet pkt;
    va_start(ap, 1);
    switch (c)
    {
    case 'j':
        {
            pkt.header.type = '4';
            chatList *p = va_arg(ap, chatList *);
            memcpy(&pkt.payload, &p->name, sizeof(p->name));
            memcpy(&pkt.payload[33], &p->peer_list[p->num_peers], sizeof(peer));
            for (int i = 0; i < p->num_peers; i++)
                send_packet(&pkt, &p->peer_list[i].addr);
            
        }
        break;
    case 'l':
        {
            pkt.header.type = '5';
            chatList *p = va_arg(ap, chatList *);
            int t = va_arg(ap, int);
            memcpy(&pkt.payload, &p->name, sizeof(p->name));
            memcpy(&pkt.payload[33], &p->peer_list[t], sizeof(peer));
            for (int i = 0; i < p->num_peers && i != t; i++)
                send_packet(&pkt, &p->peer_list[i].addr);
            
        }
        break;
    
    default:
        // Err
        break;
    }
    va_end(ap);
    
}
/* !Tracking */
/* Ping */
void * ping_output(void *ptr){
  clock_t t;
  send_pings();
  t = clock();
  while(1){
    if((float)(clock()-t)/CLOCKS_PER_SEC >= 5){ //ping time interval over
      pthread_mutex_lock(&stdout_lock);
      fprintf(stderr, "%s\n", "Checking ping responses");
      pthread_mutex_unlock(&stdout_lock);
      delete_dead_peers();
      send_pings();
      t=clock();
    }
  }
  return NULL;
}
void mark_peer(peerList *p, short a)
{
    pthread_mutex_lock(&peers_lock);
    p->alive = a;
    pthread_mutex_unlock(&peers_lock);
}
void send_pings(){
  peerList *s;
  for(s=all_peers; s != NULL; s=s->next)
  {
    mark_peer(s, 0);//mark dead
    
    packet pkt;
    pkt.header.type = 'p';
    pkt.header.error = '\0';
    pkt.header.payload_length = 0;
    int status = sendto(ping_sock, &pkt, sizeof(pkt.header), 0, (struct sockaddr *)&s->addr, sizeof(s->addr));
    if (status == -1) {
      pthread_mutex_lock(&stdout_lock);
      fprintf(stderr, "%s\n", "error - error sending packet to peer");
      pthread_mutex_unlock(&stdout_lock);
    }
  }
}
void * ping_input(void *ptr){
  socklen_t addrlen = 10;
  struct sockaddr sender_addr;
  packet recv_pkt;
  int recv_status;

  while(1)
  {
    recv_status = recvfrom(ping_sock, &recv_pkt, sizeof(recv_pkt), 0, &sender_addr, &addrlen);
    if (recv_status == -1) {
      pthread_mutex_lock(&stdout_lock);
      fprintf(stderr, "%s\n", "error - error receiving a packet, ignoring.");
      pthread_mutex_unlock(&stdout_lock);
    }else{
        if(recv_pkt.header.type == 'p'){
            peerList *s;
            for(s=all_peers; s != NULL; s=s->next)
            {
                if(memcmp(s->addr.sa_data, sender_addr.sa_data, 14) == 0
                    &&
                    memcmp(s->addr.sa_family, sender_addr.sa_family, 14) == 0)
                    {
                        mark_peer(s, 1);//mark alive
                        break;
                    }
            }
        }else
        {
            pthread_mutex_lock(&stdout_lock);
            fprintf(stderr, "%s\n", "error - received packet type unknown.");
            pthread_mutex_unlock(&stdout_lock);
        }
    }
  }
  return NULL;
}
void delete_dead_peers(){
  peerList *s = malloc(sizeof(peerList));
  peerList *r;
  for(s->next=all_peers; s->next != NULL; s=s->next){
    if(s->next->alive==0){
        r = s->next;
        s->next = r->next;
        free(r);
    }
  }
}
/* !Ping */