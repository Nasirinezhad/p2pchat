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
chatList *currentChat;

int sock;
char stat;
char name[32];

struct sockaddr_in tracker_addr;
struct sockaddr_in self_addr;

pthread_mutex_t stdout_lock;
pthread_mutex_t peers_lock;

void sets(int argc, char* argv[]);              // read args and set sock

void reply_to_ping(struct sockaddr *sender_addr);

void announces(struct sockaddr*, packet*);      // recive peer announces
void handshake(struct sockaddr*);               //! to send announces

void create_chat(char*);                        // send request to tracker server for open chat
void join_chat(char*);                          // send request to tracker to join a chat
void join_ans(packet*);                         //! response to join chat request
void alert_recv(packet*);                       // new/left member for an open chat 
void leave_chat(char*);                         // close a chat

void * read_input(void *ptr);                   // to read input in new thread
void send_msg(char*);
int send_packet(packet *, struct sockaddr *);   // send a packet to dist

void get_peers();                               // request tracker server to send list of pears
void get_chats();                               // "        "       "      "  "     "  of group chats
void peer_list(packet*);                        //! print recived list 
void chat_list(packet*);                        //!   "      "      "

char *find_addr(struct sockaddr *);             // find addr name
void echo(char*, packet*);                      // print packet 


int main(int argc, char *argv[])
{
	sets(argc, argv);
	// create a thread to read input
	pthread_t input_thread;
	pthread_create(&input_thread, NULL, read_input, NULL);
	pthread_detach(input_thread);


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
                case '1': //from tracker for chat_list
                    chat_list(&recv_pkt);
                break;
                case '2': // "      "     "   peer_list
                    peer_list(&recv_pkt);
                break;
                case '3': // "      "     "   answer_peer, joined chat success/faild
                    join_ans(&recv_pkt);
                break;
                case '4': // "      "     "   chat_alert, new/left chat member
                    alert_recv(&recv_pkt);
                break;
                case 'p': //ping
                    reply_to_ping(&sender_addr);
                break;
                case 'a': // somebody want open private chat
                    announces(&sender_addr, &recv_pkt);
                break;
                case 'l': // sombody leave chat
                    leave_chat(&sender_addr, &recv_pkt);
                break;
                case 'm': // message
                    echo(find_addr(&sender_addr), &recv_pkt);
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
    //get data
    short tracker_port, self_port;
    char tracker_ip[16];
    if(argc > 3){
        strcpy(tracker_ip, argv[1], 15);
	    tracker_port = atoi(argv[2]);
	    self_port = atoi(argv[3]);
    }else{
        printf("Enter ServerIP ServerPort SelfPort Name (192.168.2.2 8080 8050 Mahdi)");
        scanf("%s %d %d %s", tracker_ip, tracker_port, self_port, name);
    }
    // set addrs
	self_addr.sin_family = AF_INET; 
	self_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	self_addr.sin_port = htons(self_port);

	tracker_addr.sin_family = AF_INET; 
	if (inet_aton(tracker_ip, &tracker_addr.sin_addr) == 0) {
		fprintf(stderr, "%s\n", "error - error parsing tracker ip.");
		abort();
	}
	tracker_addr.sin_port = htons(tracker_port);

    //announces to server
    handshake((struct sockaddr*)&tracker_addr);

    //done
    chats = NULL;
    currentChat = NULL;
    stat = 1;
}

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

void reply_to_ping(struct sockaddr *sender_addr) {
	// new packet
	packet pkt;
	pkt.header.type = 'p';
	pkt.header.error = '\0';
	pkt.header.payload_length = 0;

	// send ping reply
	int status = sendto(sock, &pkt, sizeof(pkt.header), 0, &sender_addr, sizeof(struct sockaddr));
	if (status == -1) {
		pthread_mutex_lock(&stdout_lock);
		fprintf(stderr, "error - error replying to ping message, possibility of being opt-out.");
		pthread_mutex_unlock(&stdout_lock);
	}
}

void * read_input(void *ptr)
{

	char line[PAYLOAD_SIZE];
	char *p;
	while (1) {
		// read input
		memset(line, 0, sizeof(line));
		p = fgets(line, sizeof(line), stdin);
		// flush input stream to clear out long message
		if (p == NULL) {
			pthread_mutex_lock(&stdout_lock);
			fprintf(stderr, "%s\n", "error - cannot read input");
			pthread_mutex_unlock(&stdout_lock);
			continue;
		}
		if (line[strlen(line) - 1] != '\n') {
			// flush input stream to clear out long message
			scanf ("%*[^\n]"); 
			(void) getchar ();
		}
		line[strlen(line) - 1] = '\0';

		// parse input
        if (line[0] == '/') // if message was a commond
        {
            line[0] = ' ';
            switch (line[1])
            {
            case 'c':
                line[1] = ' ';
                create_chat(line);
                break;
            case 'j':
                line[1] = ' ';
                join_chat(line);
                break;
            case 'l':
                line[1] = ' ';
                leave_chat(line);
                break;
            case 'g':
                line[1] = ' ';
                get_chats(line);
                break;
            case 'p':
                line[1] = ' ';
                get_peers(line);
                break;
            
            default:
                break;
            }
        }else{
            send_msg(line);
        }
  return NULL;
}

void announces(struct sockaddr *addr, packet *pkt)
{
    chatList *p;
    if (chats == NULL){
        chats = malloc(sizeof(chatList));
        p = chats;
    }else{
        while (p->next != NULL)
            p = p->next;
        p->next = malloc(sizeof(chatList));
        p = p->next;
    }

    if (sscanf(pkt->payload, "%s", p->name) < 1)
    {
        struct sockaddr_in *addri = (struct sockaddr_in *)addr;
        unsigned int ip = addri->sin_addr.s_addr;
        short port = htons(addri->sin_port);
        sprintf(p->name, "%d:%d", ip, port);
    }
    p->max_peer = 1;

    p->peer_list = (peer *) malloc(sizeof(peer));

    p->peer_list[0].addr = *addr;
    p->peer_list[0].alive = 1;

    //currentChat = p;
}

void handshake(struct sockaddr *addr)
{
    packet pkt;
	pkt.header.type = 'a';
	pkt.header.payload_length = 32;
    strcpy(pkt.payload, name, 32);
    send_packet(addr, &pkt);
}

void create_chat(char *line)
{
    int skip;
    packet pkt;
    if (sscanf(line, "   %s %d %s", skip, skip, skip) > 1)
    {
        pkt->header.type = 'r';
        pkt->header.payload_length = strlen(line)-3;
        memcpy(&pkt->payload, &line[3], pkt->header.payload_length);
        send_packet(pkt, (struct sockaddr*)&tracker_addr);
    }
}

void join_chat(char *line)
{
    int skip;
    packet pkt;
    if (sscanf(line, "   %s %d %s", skip, skip, skip) > 1)
    {
        pkt->header.type = 'j';
        pkt->header.payload_length = strlen(line)-3;
        memcpy(&pkt->payload, &line[3], pkt->header.payload_length);
        send_packet(pkt, (struct sockaddr*)&tracker_addr);
    }
}

void leave_chat(char *line)
{
    int skip;
    packet pkt;
    if (sscanf(line, "   %s %d", skip, skip) == 2)
    {
        pkt.header.type = 'j';
        pkt.header.payload_length = strlen(line)-3;
        memcpy(&pkt.payload, &line[3], pkt.header.payload_length);
        send_packet(pkt, (struct sockaddr*)&tracker_addr);
    }
}

void send_msg(char *msg)
{
    if (currentChat == NULL)
    {
        //Err
        return;
    }

    pkt.header.type = 'm';
    pkt.header.payload_length = strlen(msg);
    memcpy(&pkt->payload, msg, pkt->header.payload_length);
    send_packet(pkt, currentChat->addr);
}
void get_peers()
{
    pkt->header.type = 'p';
    pkt->header.payload_length = 0;
    send_packet(pkt, currentChat->addr);
    /* TODO: 
     * keep stat and wait to recive list
     */
}
void get_chats()
{
    pkt->header.type = 'c';
    pkt->header.payload_length = 0;
    send_packet(pkt, currentChat->addr);
    /* TODO: 
     * keep stat and wait to recive list
     */
}

void echo(char *sr, packet *pkt)
{
    if (sr == NULL)
        return;     //drop unknow source

    if(pkt->type == 'm'){
        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "%s: %s\n", sr, pkt->payload);
        pthread_mutex_unlock(&stdout_lock);
    }
}

char *find_addr(struct sockaddr *addr)
{
    if (currentChat == NULL)
    {
        //Err
        return NULL;
    }
    for (int i = 0; i < currentChat->num_peers; i++)
    {
        if (memcmp(currentChat->peer_list[i].addr, addr) == 0)
        {
            return &currentChat->peer_list[i].name;
        }
    }
    /* TODO:
     * Check if addr was in another chat
     */
    return NULL;
}

void join_ans(packet *pkt)
{
    if (pkt->header.error == '\0')
    {
        chatList *p;
        if (chats == NULL){
            chats = malloc(sizeof(chatList));
            p = chats;
        }else{
            while (p->next != NULL)
                p = p->next;
            p->next = malloc(sizeof(chatList));
            p = p->next;
        }
        memcpy(p, &pakt->payload, sizeof(chatList));
        p->next = NULL;
        currentChat = p;
    }
}

void alert_recv(packet *pkt)
{
    char n[32];
    chatList *p = NULL;
    if (chats == NULL){
        //Err
        return;
    }

    memcpy(&n, &pkt->payload, sizeof(n));
    
    if (currentChat != NULL && strcpy(currentChat->name, n) == 0)
    {
        p = currentChat;    
    }else
    {
        p = chats;
        while (p != NULL && strcpy(currentChat->name, n) != 0)
            p = p->next;
    }
    if (p == NULL)
    {
        // Err
        return;
    }
    if(pkt->header.type == 'j')
    {
        p->num_peers++;
        memcpy(&p->peer_list[p->num_peers], &pkt->payload[33], sizeof(peer));
    }else if (pkt->header.type == 'l')
    {
        peer t;
        bool f=false;
        memcpy(&t, &pkt->payload[33], sizeof(peer));
        for(int i=0; i < p->num_peers; i++){
            if(memcpy(p->peer_list[i], t)==0)
                f=true;
            if (f)
                p->peer_list[i] = p->peer_list[i+1];
        }
        p->num_peers--;
    }
}

void peer_list(packet *pkt)
{
    peer *buff;
    peerList *peers = NULL, *p;
    int i = 1;

    FILE *stream;
    stream = open_memstream (&pkt->payload, &pkt->header.payload_length);
    while (!feof(stream))
    {
        buff = malloc(sizeof(peer));
        fscanf(stream, "%s %s\n", buff->name, buff->addr);
        if(peers == NULL){
            peers = buff;
        }else{
            p->next = buff;
        }
        p = buff;

        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "%d: %s\n", i, buff->name);
        pthread_mutex_unlock(&stdout_lock);

        i++;
    }
    /* TODO:
     * wait for select a peer to connect
     */
}
void chat_list(packet *pkt)
{
    char buff[32];
    int i = 1;

    FILE *stream;
    stream = open_memstream (&pkt->payload, &pkt->header.payload_length);
    while (!feof(stream))
    {
        fscanf(stream, "%s %s\n", buff);
        
        pthread_mutex_lock(&stdout_lock);
        fprintf(stderr, "%d: %s\n", i, buff);
        pthread_mutex_unlock(&stdout_lock);

        i++;
    }
    /* TODO:
     * wait for select a chat to connect
     */
}