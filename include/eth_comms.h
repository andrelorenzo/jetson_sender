 /**
 * @file eth_comms.h
 * @author André Lorenzo Torres (andrelorenzotorres@gmail.com)
 * @brief UDP and TCP simple implementation (Windows, LInux and STM32 compliant)
 * @version 0.1
 * @date 19-12-2025
 * 
 */
#ifndef ETH_COMMS_H_
#define ETH_COMMS_H_

#include <arpa/inet.h>
#include "stdlib.h"
#include <unistd.h>
#include "fcntl.h"
#include "string.h"
#include "stdbool.h"
#include "stdio.h"
#include "pthread.h"
#include "stdarg.h"
#include "errno.h"
#include <signal.h>
#include "stdint.h"
#include <time.h>

#ifndef COMMS_MAX_CLIENTS
#define COMMS_MAX_CLIENTS 256
#endif // COMMS_MAX_CLIENTS

#ifndef COMMS_DEF_LISTEN_PORT
#define COMMS_DEF_LISTEN_PORT   5000
#endif // COMMS_DEF_LISTEN_PORT

#ifndef COMMS_DEF_LISTEN_IP
#define COMMS_DEF_LISTEN_IP     "0.0.0.0"
#endif // COMMS_DEF_LISTEN_IP

#define COMMS_SEND_TO_ALL_CLIENTS 65535


#define CONFIG_IPV4   (0x00u)
#define CONFIG_IPV6   (0x01u)

typedef enum{
    COMMS_DEBUG = 0U,
    COMMS_INFO,
    COMMS_WARN,
    COMMS_ERROR,
    COMMS_FATAL,
    COMMS_NONE
}comms_verb_e;

typedef struct commh_t commh_t;
typedef struct comms_opt_t comms_opt_t;
typedef struct comms_send_opt_t comms_send_opt_t;

static comms_verb_e verbosity = COMMS_WARN;
typedef void (*comm_recvcb_t)(uint8_t *msg, size_t len, const char *ip, uint16_t port, uint16_t cid);



#define CommsTCPServerInit(commh, ...)  comms_tcpserver_init__opt((commh), (comms_opt_t){__VA_ARGS__})
#define CommsTCPClientInit(commh, ...)  comms_tcpclient_init__opt((commh), (comms_opt_t){__VA_ARGS__})
#define CommsUDPInit(commh,...)         comms_udp_init__opt((commh), (comms_opt_t){__VA_ARGS__})
#define CommsSend(commh, msg, len,...)  comms_send__opt((commh), (msg), (len), (comms_send_opt_t){__VA_ARGS__})

bool CommsConnect(commh_t * commh, const char * ip, uint16_t port); /* TCP client or UDP */
bool CommsDisconnect(commh_t *commh, uint16_t cid); /* TCP server */
bool CommsClose(commh_t *commh);

void CommsPrintClients(commh_t * commh, FILE * stream); /* TCP server */

void CommsExit(commh_t * commh);
void CommsPrintError(commh_t * commh, FILE * output);
void CommsLogSetVerbosity(comms_verb_e verb);
void CommsLogSetStream(commh_t * commh, FILE * output);
void CommsLogSetOff(commh_t * commh);
void CommsLog(comms_verb_e verb, FILE * stream, const char * fmt, ...);



#ifdef ETHCOMMS_IMP


typedef enum{
    COMMS_ERROR_NOERROR  = 0U,
    COMMS_ERROR_SOCKET,
    COMMS_ERROR_BIND,
    COMMS_ERROR_LISTEN,
    COMMS_ERROR_ACCEPT,
    COMMS_ERROR_CONNECT,
    COMMS_ERROR_NO_REMOTE_PROVIDED,
    COMMS_ERROR_FAILED_TO_SEND,
    COMMS_ERROR_FAILED_TO_CREATE_LISTENER,
    COMMS_ERROR_FAILED_TO_CREATE_ACCEPTER,
    COMMS_ERROR_CALLBACK_NOT_PROVIDED,
    COMMS_ERROR_MAX_CLIENTS,
    COMMS_ERROR_CLOSE_COMMS,
    COMMS_ERROR_DISCONNECT,
    COMMS_ERROR_CONTEXT_NOT_PROVIDED,

    COMMS_ERROR__COUNT
}comms_error_t;
typedef struct comms_opt_t{
    bool ipv6;
    char * local_ip;
    uint16_t local_port;
    comm_recvcb_t recvcb;
}comms_opt_t;


typedef struct comms_send_opt_t{
    char * ip;
    uint16_t port;
    uint16_t cid;
}comms_send_opt_t;

typedef struct commh_t{
    // TCP
    bool tcp;
    bool server;
    int clients_socks[COMMS_MAX_CLIENTS];
    union{
        struct sockaddr_in asv4[COMMS_MAX_CLIENTS];
        struct sockaddr_in6 asv6[COMMS_MAX_CLIENTS];
    }client_address;
    int client_count;
    ////

    
    union{
        struct sockaddr_in asv4;
        struct sockaddr_in6 asv6;
    }local_address;
    
    comm_recvcb_t recvcb;
    pthread_t listener;
    pthread_t accepter;

    volatile bool running;
    FILE * stream;
    comms_error_t err_type;
    char error[256];
}commh_t;

#define UNUSED(value) (void)(value)
#define TODO(message) do { fprintf(stderr, "%s:%d: TODO: %s\n", __FILE__, __LINE__, message); abort(); } while(0)
#define UNREACHABLE(message) do { fprintf(stderr, "%s:%d: UNREACHABLE: %s\n", __FILE__, __LINE__, message); abort(); } while(0)

static pthread_mutex_t g_comms_mtx = PTHREAD_MUTEX_INITIALIZER;


static void _set_error(commh_t * ctx, uint8_t err,const char * f, int l);

bool comms_tcpserver_init__opt(commh_t *commh, comms_opt_t opt);
bool comms_tcpclient_init__opt(commh_t *commh, comms_opt_t opt);
bool comms_udpinit__opt(commh_t * commh, comms_opt_t opt);
bool comms_send__opt(commh_t * commh, uint8_t * msg, size_t len, comms_send_opt_t send_opt);

void CommsLogSetVerbosity(comms_verb_e verb){verbosity = verb;}
void CommsLogSetStream(commh_t * commh, FILE * output){commh->stream = output;}
void CommsLogSetOff(commh_t * commh){commh->stream = NULL;}

void CommsLog(comms_verb_e verb, FILE * stream, const char * fmt, ...){
    if(verb < verbosity || stream == NULL){
        return;
    }
    va_list args;
    va_start(args, fmt);
    char msg[128];
    vsnprintf(msg, sizeof msg, fmt, args);
    char ident[256];
    switch(verb){
        case COMMS_DEBUG:snprintf(ident, sizeof ident, "\x1b[2m\x1b[90m[DEBUG]\x1b[0m:");break;
        case COMMS_INFO:snprintf(ident, sizeof ident, "\x1b[32m[INFO]\x1b[0m:");break;
        case COMMS_WARN:snprintf(ident, sizeof ident, "\x1b[33m[WARN]\x1b[0m:");break;
        case COMMS_ERROR:snprintf(ident, sizeof ident, "\x1b[31m[ERROR]\x1b[0m:");break;
        case COMMS_FATAL:snprintf(ident, sizeof ident, "\x1b[35m[FATAL]\x1b[0m:");break;
        case COMMS_NONE:snprintf(ident, sizeof ident, "\x1b[34m[NONE]\x1b[0m:");break;
        default: UNREACHABLE("CommsLog");
    }
    strcat(ident, msg);
    fprintf(stream, "%s", ident);

    va_end(args);
}


void CommsPrintClients(commh_t * commh, FILE * stream){
    if (!commh) {
        CommsLog(COMMS_ERROR, commh->stream,"[%s] : Context not provided!\n", __func__);
        _set_error(commh, COMMS_ERROR_CONTEXT_NOT_PROVIDED, __func__, __LINE__);
        return;
    }


    fprintf(stream, "---------------------------------\n");
    char ip[16];
    inet_ntop(AF_INET, &commh->client_address.asv4[0].sin_addr, ip, sizeof(ip));
    uint16_t port = ntohs(commh->client_address.asv4[0].sin_port);
    fprintf(stream, "> LOCAL: %s:%u\n", ip, port);
    fprintf(stream, "--------Connected clients (%s %s)--------\n", commh->tcp ? "TCP": "UDP", commh->server ? "server" : "");
    if(commh->client_count == 1){
        CommsLog(COMMS_WARN, stream, "No clients connected\n");
    }
    for(int i = 1; i < commh->client_count; ++i){
        inet_ntop(AF_INET, &commh->client_address.asv4[i].sin_addr, ip, sizeof(ip));
        port = ntohs(commh->client_address.asv4[i].sin_port);
        fprintf(stream, "> Client (%i) : %s:%u\n", i, ip, port);
    }
    fprintf(stream, "---------------------------------\n");

}

bool CommsDisconnect(commh_t *commh, uint16_t cid){
    if(!commh) return false;

    if(!commh->tcp || !commh->server){
        _set_error(commh, COMMS_ERROR_DISCONNECT, __func__, __LINE__);
        return false;
    }

    pthread_mutex_lock(&g_comms_mtx);

    if(cid < 1 || cid >= (uint16_t)commh->client_count){
        pthread_mutex_unlock(&g_comms_mtx);
        _set_error(commh, COMMS_ERROR_DISCONNECT, __func__, __LINE__);
        return false;
    }

    int sock = commh->clients_socks[cid];
    if(sock <= 0){
        pthread_mutex_unlock(&g_comms_mtx);
        _set_error(commh, COMMS_ERROR_DISCONNECT, __func__, __LINE__);
        return false;
    }

    struct sockaddr_in old = commh->client_address.asv4[cid];
    if(commh->clients_socks[cid] > 0){
        (void)shutdown(commh->clients_socks[cid], SHUT_RDWR);
        (void)close(commh->clients_socks[cid]);
    }
    commh->clients_socks[cid] = 0;
    memset(&commh->client_address.asv4[cid], 0, sizeof(commh->client_address.asv4[cid]));

    for(int i = (int)cid; i < commh->client_count - 1; ++i){
        commh->clients_socks[i]      = commh->clients_socks[i+1];
        commh->client_address.asv4[i]= commh->client_address.asv4[i+1];
    }

    commh->clients_socks[commh->client_count - 1] = 0;
    memset(&commh->client_address.asv4[commh->client_count - 1], 0,
           sizeof(commh->client_address.asv4[0]));

    commh->client_count--;

    pthread_mutex_unlock(&g_comms_mtx);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &old.sin_addr, ip, sizeof(ip));
    uint16_t port = ntohs(old.sin_port);
    CommsLog(COMMS_INFO, commh->stream, "[%s] Client disconnected %s:%u (cid=%u)\n",
             __func__, ip, port, cid);

    return true;
}




bool CommsClose(commh_t *commh){
    if (!commh) {
        CommsLog(COMMS_ERROR, stderr,"[%s] : Context not provided!\n", __func__);
        return false;
    }

    if(commh->client_count > 0){
        for(int i = 0; i < commh->client_count; ++i){
            if(commh->clients_socks[i] > 0){
                (void)shutdown(commh->clients_socks[i], SHUT_RDWR);
                (void)close(commh->clients_socks[i]);
                commh->clients_socks[i] = -1;
                memset(&commh->client_address.asv4[i], 0, sizeof(commh->client_address.asv4[i]));
            }
            
        }
    }
    
    commh->running = false;
    int rc = pthread_join(commh->listener, NULL);
    if (rc != 0) {
        _set_error(commh, COMMS_ERROR_CLOSE_COMMS, __func__, __LINE__);
        CommsLog(COMMS_ERROR, commh->stream, "[%s] Listener Thread could not be closed\n", __func__);
        return false;
    }
    if(commh->server){
        rc = pthread_join(commh->accepter, NULL);
        if (rc != 0) {
            _set_error(commh, COMMS_ERROR_CLOSE_COMMS, __func__, __LINE__);
            CommsLog(COMMS_ERROR, commh->stream, "[%s] Accepter Thread could not be closed\n", __func__);
            return false;
        }
    }
    

    CommsLog(COMMS_INFO, commh->stream,"[%s] Comunication closed succesfully!\n", __func__);
    return true;
}


bool CommsConnect(commh_t * commh, const char * ip, uint16_t port){
    if(commh->server){
        _set_error(commh, COMMS_ERROR_CONNECT, __func__, __LINE__);
        CommsLog(COMMS_ERROR, commh->stream,"[%s] : Connect cannot be done as a TCP server!\n", __func__);
        return false;
    }

    if (!commh) {
        CommsLog(COMMS_ERROR, stderr,"[%s] : Context not provided!\n", __func__);
        return false;
    }

    if(port == 0){
        CommsLog(COMMS_ERROR, commh->stream,"[%s] Port not provided!\n", __func__);
    }

    if(ip == NULL){
        CommsLog(COMMS_ERROR, commh->stream,"[%s] IP not provided!\n", __func__);   
    }

    struct sockaddr_in addr;
        addr.sin_family =AF_INET;
        addr.sin_addr.s_addr = inet_addr(ip);
        // .sin_port = commh->local_address.asv4.sin_port,
        addr.sin_port = htons(port);

    int i = 0;
    if(commh->tcp){
        int ret = connect(commh->clients_socks[0], (struct sockaddr *)&addr, (socklen_t)sizeof(struct sockaddr_in));
        if(ret < 0){
            _set_error(commh, COMMS_ERROR_CONNECT, __func__, __LINE__);
            CommsLog(COMMS_WARN, commh->stream,"[%s] client could not connect!\n", __func__);   
            return false;
        }
        
        i = commh->client_count; // 0 is listening socket
        commh->client_count++;
    }
    commh->client_address.asv4[i].sin_addr.s_addr = inet_addr(ip);
    commh->client_address.asv4[i].sin_port = htons(port);
    if(commh->client_count == 0){
        commh->client_count = 1;
    }
    CommsLog(COMMS_INFO, commh->stream,"[%s] Client connected succesfully !\n", __func__);   
    return true;
}



void CommsPrintError(commh_t * commh, FILE * output){
    if (!commh) {
        CommsLog(COMMS_ERROR, stderr,"[%s] : Context not provided!\n", __func__);
        return;
    }

    switch(commh->err_type){
        case COMMS_ERROR_NOERROR: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_NOERROR\n", commh->error);break;
        case COMMS_ERROR_SOCKET: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_SOCKET\n", commh->error);break;
        case COMMS_ERROR_BIND: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_BIND\n", commh->error);break;
        case COMMS_ERROR_LISTEN: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_LISTEN\n", commh->error);break;
        case COMMS_ERROR_ACCEPT: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_ACCEPT\n", commh->error);break;
        case COMMS_ERROR_CONNECT: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_CONNECT\n", commh->error);break;
        case COMMS_ERROR_NO_REMOTE_PROVIDED: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_NO_REMOTE_PROVIDED\n", commh->error);break;
        case COMMS_ERROR_FAILED_TO_SEND: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_FAILED_TO_SEND\n", commh->error);break;
        case COMMS_ERROR_FAILED_TO_CREATE_LISTENER: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_FAILED_TO_CREATE_LISTENER\n", commh->error);break;
        case COMMS_ERROR_CALLBACK_NOT_PROVIDED: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_CALLBACK_NOT_PROVIDED\n", commh->error);break;
        case COMMS_ERROR_CLOSE_COMMS: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_CLOSE_COMMS\n", commh->error);break;
        case COMMS_ERROR_MAX_CLIENTS: CommsLog(COMMS_ERROR,output,"(%s)-> COMMS_ERROR_MAX_CLIENTS\n", commh->error);break;
        default:UNREACHABLE("CommsPrintError");break;
    }
}

void CommsExit(commh_t * commh){
    if (!commh) {
        CommsLog(COMMS_ERROR, stderr,"[%s] : Context not provided!\n", __func__);
        return;
    }

    CommsPrintError(commh, stderr);
    exit(-1);
}

bool comms_send__opt(commh_t * commh, uint8_t * msg, size_t len, comms_send_opt_t send_opt){
    if (!commh) {
        CommsLog(COMMS_ERROR, stderr,"[%s] : Context not provided!\n", __func__);
        return false;
    }

    ssize_t written = 0;
    if(commh->tcp){
        int cid = send_opt.cid;

        int sock = -1;
        if(commh->server){
            if(cid < 1){
                _set_error(commh, COMMS_ERROR_FAILED_TO_SEND, __func__, __LINE__);
                CommsLog(COMMS_ERROR, stderr,"[%s] : A valid cid (0 < cid < %i) must be selected!\n", __func__, COMMS_MAX_CLIENTS);
                return false;
            }
            if(cid != COMMS_SEND_TO_ALL_CLIENTS){
                sock = commh->clients_socks[cid];
            }else{
                for(int i = 1; i < commh->client_count; ++i){
                    written = send(commh->clients_socks[i], msg, len, 0);
                    char * ip = inet_ntoa(commh->client_address.asv4[i].sin_addr);
                    uint16_t port = ntohs(commh->client_address.asv4[i].sin_port);
                    if(written <= 0){
                        _set_error(commh, COMMS_ERROR_FAILED_TO_SEND, __func__, __LINE__);
                        CommsLog(COMMS_ERROR, commh->stream,"[%s]Failed to send TCP message to %s:%u!\n", __func__, ip,port);
                        return false;
                    }
                    CommsLog(COMMS_DEBUG, commh->stream,"[%s] Succesfully sent (%li bytes) over TCP to %s:%u !\n", __func__,written, ip, port);
                }
                CommsLog(COMMS_INFO, commh->stream,"[%s] Succesfully sent (%li bytes) over TCP to all %i clients!\n", __func__,written, commh->client_count-1);
                return true;

            }
            
        } else {
            if(cid == COMMS_SEND_TO_ALL_CLIENTS){
                _set_error(commh, COMMS_ERROR_FAILED_TO_SEND, __func__, __LINE__);
                CommsLog(COMMS_ERROR, stderr,"[%s] : A valid cid (0 < cid < %i) must be selected!\n", __func__, COMMS_MAX_CLIENTS);      
                return false;
            }
            sock = commh->clients_socks[0];
            cid = 0;
        }

        if(sock < 0){
            _set_error(commh, COMMS_ERROR_NO_REMOTE_PROVIDED, __func__, __LINE__);
            return false;
        }
        written = send(sock, msg, len, 0);
        char * ip = inet_ntoa(commh->client_address.asv4[cid].sin_addr);
        uint16_t port = ntohs(commh->client_address.asv4[cid].sin_port);

        if(written <= 0){
            _set_error(commh, COMMS_ERROR_FAILED_TO_SEND, __func__, __LINE__);
            CommsLog(COMMS_ERROR, commh->stream,"[%s]Failed to send TCP message to %s:%u!\n", __func__, ip,port);
            return false;
        }
        CommsLog(COMMS_INFO, commh->stream,"[%s] Succesfully sent (%li bytes) over TCP to %s:%u !\n", __func__,written, ip, port);
        return true;
    }else{
        if((send_opt.port == 0 || send_opt.ip == NULL) && commh->client_count == 0){
            _set_error(commh, COMMS_ERROR_NO_REMOTE_PROVIDED, __func__, __LINE__);
            CommsLog(COMMS_ERROR, commh->stream,"[%s] Client connected succesfully !\n", __func__);
            return false;
        }else if(send_opt.port != 0 && commh->client_count > 0){
            commh->client_address.asv4[0].sin_port = htons(send_opt.port);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] Port changed to %u!\n", __func__, send_opt.port);
        }else if(send_opt.ip != NULL && commh->client_count > 0){
            commh->client_address.asv4[0].sin_addr.s_addr = inet_addr(send_opt.ip);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] IP changed to %s !\n", __func__, send_opt.ip);
        }else if(commh->client_count == 0){
            commh->client_address.asv4[0].sin_addr.s_addr = inet_addr(send_opt.ip);
            commh->client_address.asv4[0].sin_port = htons(send_opt.port);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] Remote changed to %s:%u !\n", __func__, send_opt.ip, send_opt.port);
        }
        commh->client_address.asv4[0].sin_family = AF_INET;
        written = sendto(commh->clients_socks[0], (const void*)msg, len, 0, (const struct sockaddr*)&commh->client_address.asv4[0], (socklen_t)sizeof(struct sockaddr_in));
        char * ip = inet_ntoa(commh->client_address.asv4[0].sin_addr);
        uint16_t port = ntohs(commh->client_address.asv4[0].sin_port);

        if(written <= 0){
            _set_error(commh, COMMS_ERROR_FAILED_TO_SEND, __func__, __LINE__);
            CommsLog(COMMS_ERROR, commh->stream,"[%s]Failed to send UDP message to %s:%u!\n", __func__, ip,port);
            return false;
        }
        CommsLog(COMMS_INFO, commh->stream,"[%s] Succesfully sent (%li bytes) over UDP to %s:%u !\n", __func__,written, ip, port);
        return true;
    }


}

void * comms__listener(void * arg){
    commh_t *commh = (commh_t*)arg;

    if(commh->clients_socks[0] > 0){
        int fl = fcntl(commh->clients_socks[0], F_GETFL, 0);
        fcntl(commh->clients_socks[0], F_SETFL, fl | O_NONBLOCK);
    }

    CommsLog(COMMS_DEBUG, commh->stream,"[%s] Listener started (%s)\n",
             __func__, commh->tcp ? "TCP" : "UDP");

    uint8_t msg[2048];

    while(commh->running){
        if(commh->tcp){
            if(commh->server){
                int local_count = 0;
                pthread_mutex_lock(&g_comms_mtx);
                local_count = commh->client_count;
                pthread_mutex_unlock(&g_comms_mtx);

                bool got_any = false;

                for(int i = 1; i < local_count; ++i){
                    int fd = 0;
                    struct sockaddr_in addr;

                    pthread_mutex_lock(&g_comms_mtx);
                    fd = commh->clients_socks[i];
                    addr = commh->client_address.asv4[i];
                    pthread_mutex_unlock(&g_comms_mtx);

                    if(fd <= 0) continue;

                    ssize_t n = recv(fd, msg, sizeof msg, 0);
                    if(n > 0){
                        got_any = true;
                        char ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
                        uint16_t port = ntohs(addr.sin_port);

                        if(commh->recvcb) commh->recvcb(msg, (size_t)n, ip, port, (uint16_t)i);
                    }else if(n == 0){
                        (void)CommsDisconnect(commh, (uint16_t)i);
                        got_any = true;
                    }else{
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                        }else if(errno == EINTR){
                            got_any = true;
                        }else{
                            (void)CommsDisconnect(commh, (uint16_t)i);
                            got_any = true;
                        }
                    }
                }

                if(!got_any) usleep(1000);
            }else{
                // TCP client
                int fd = 0;
                pthread_mutex_lock(&g_comms_mtx);
                fd = commh->clients_socks[0];
                pthread_mutex_unlock(&g_comms_mtx);

                if(fd <= 0){
                    usleep(1000);
                    continue;
                }

                ssize_t n = recv(fd, msg, sizeof msg, 0);
                if(n > 0){
                    char ip[INET_ADDRSTRLEN];
                    pthread_mutex_lock(&g_comms_mtx);
                    struct sockaddr_in addr = commh->client_address.asv4[0];
                    pthread_mutex_unlock(&g_comms_mtx);

                    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
                    uint16_t port = ntohs(addr.sin_port);
                    if(commh->recvcb) commh->recvcb(msg, (size_t)n, ip, port, 0);
                }else if(n == 0){
                    (void)CommsDisconnect(commh, 0);
                    usleep(1000);
                }else{
                    if(errno == EAGAIN || errno == EWOULDBLOCK){
                        usleep(1000);
                    }else if(errno == EINTR){
                        // retry
                    }else{
                        (void)CommsDisconnect(commh, 0);
                        usleep(1000);
                    }
                }
            }
        }else{
            // UDP
            int fd = 0;
            pthread_mutex_lock(&g_comms_mtx);
            fd = commh->clients_socks[0];
            pthread_mutex_unlock(&g_comms_mtx);

            if(fd <= 0){
                usleep(1000);
                continue;
            }

            struct sockaddr_in src;
            socklen_t src_len = sizeof(src);
            ssize_t n = recvfrom(fd, msg, sizeof msg, 0, (struct sockaddr*)&src, &src_len);

            if(n > 0){
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &src.sin_addr, ip, sizeof(ip));
                uint16_t port = ntohs(src.sin_port);
                if(commh->recvcb) commh->recvcb(msg, (size_t)n, ip, port, 0);
            }else if(n < 0){
                if(errno == EAGAIN || errno == EWOULDBLOCK){
                    usleep(1000);
                }else if(errno == EINTR){
                    // retry
                }else{
                    CommsLog(COMMS_WARN, commh->stream, "[%s] recvfrom errno=%d\n", __func__, errno);
                    usleep(1000);
                }
            }
        }
    }

    CommsLog(COMMS_DEBUG, commh->stream,"[%s] Listener stopped\n", __func__);
    return NULL;
}


void * comms_accepter(void * arg){
    commh_t *commh = (commh_t*)arg;
    CommsLog(COMMS_DEBUG, commh->stream,"[%s] Accepter started\n", __func__);

    int fl = fcntl(commh->clients_socks[0], F_GETFL, 0);
    fcntl(commh->clients_socks[0], F_SETFL, fl | O_NONBLOCK);

    while(commh->running){
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);

        int cs = accept(commh->clients_socks[0], (struct sockaddr*)&cli, &cli_len);
        if(cs < 0){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                usleep(1000);
                continue;
            }
            if(errno == EINTR) continue;

            if(!commh->running && (errno == EBADF || errno == EINVAL)){
                break;
            }

            CommsLog(COMMS_WARN, commh->stream, "[%s] accept error errno=%d\n", __func__, errno);
            usleep(1000);
            continue;
        }

        int fl = fcntl(cs, F_GETFL, 0);
        fcntl(cs, F_SETFL, fl | O_NONBLOCK);

        pthread_mutex_lock(&g_comms_mtx);

        if(commh->client_count >= COMMS_MAX_CLIENTS){
            pthread_mutex_unlock(&g_comms_mtx);
            _set_error(commh, COMMS_ERROR_MAX_CLIENTS, __func__, __LINE__);
            CommsLog(COMMS_ERROR, commh->stream,"[%s] Max clients reached\n", __func__);
            if(cs > 0){
                (void)shutdown(cs, SHUT_RDWR);
                (void)close(cs);
            }
            cs = 0;
            break;
        }

        int idx = commh->client_count;
        commh->clients_socks[idx] = cs;
        commh->client_address.asv4[idx] = cli;
        commh->client_count++;

        pthread_mutex_unlock(&g_comms_mtx);

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        uint16_t port = ntohs(cli.sin_port);
        CommsLog(COMMS_INFO, commh->stream,"[%s] New client %s:%u (cid=%d)\n",
                 __func__, ip, port, idx);
    }

    CommsLog(COMMS_DEBUG, commh->stream,"[%s] Accepter stopped\n", __func__);
    return NULL;
}

bool comms_udp_init__opt(commh_t * commh, comms_opt_t opt){
    if (!commh) {
        CommsLog(COMMS_ERROR, stderr,"[%s] : Context not provided!\n", __func__);
        return false;
    }
    commh->stream = stdout;

    int domain = AF_INET;

    if(opt.ipv6){
        //TODO("ipV6 is not implemented yet");
        // domain = AF_INET6;
        // commh->local_address.asv6.sin6_family = AF_INET6;
    }else{  // IPV4
        commh->local_address.asv4.sin_family = AF_INET;

        if(opt.local_port != 0){
            commh->local_address.asv4.sin_port = htons(opt.local_port);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] Port added: %u!\n", __func__, opt.local_port);
        }else{   
            commh->local_address.asv4.sin_port = htons(COMMS_DEF_LISTEN_PORT);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] Port set as default: %u !\n ", __func__, COMMS_DEF_LISTEN_PORT);
        }

        if(opt.local_ip != NULL){
            commh->local_address.asv4.sin_addr.s_addr = inet_addr(opt.local_ip);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] IP added: %s!\n", __func__, opt.local_ip);
        }else{   
            commh->local_address.asv4.sin_addr.s_addr = inet_addr(COMMS_DEF_LISTEN_IP);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] IP set as default: %s!\n", __func__, COMMS_DEF_LISTEN_IP);
        }
    }

    int sock = socket(domain,SOCK_DGRAM,0);
    if(sock < 0){
        _set_error(commh, COMMS_ERROR_SOCKET, __func__, __LINE__);
        CommsLog(COMMS_ERROR, commh->stream,"[%s] Socket could not be created!\n", __func__);
        return false;
    }
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    commh->clients_socks[0] = sock;
    int err = bind(commh->clients_socks[0], (const struct sockaddr*)&commh->local_address, (socklen_t) sizeof(struct sockaddr_in));

    if(err < 0 ){
        commh->clients_socks[0] = -1;
        _set_error(commh, COMMS_ERROR_BIND, __func__, __LINE__);
        CommsLog(COMMS_ERROR, commh->stream,"[%s] Binding could not be done on socket!\n", __func__);
        return false;
    }

    commh->recvcb = opt.recvcb;
    commh->client_count = 0;
    commh->running = (commh->recvcb != NULL);
    commh->tcp = false;
    commh->server = false;

    if (commh->running) {
        
        int rc = pthread_create(&commh->listener, NULL, comms__listener, commh);
        if (rc != 0) {
            _set_error(commh, COMMS_ERROR_FAILED_TO_CREATE_LISTENER, __func__, __LINE__);
            CommsLog(COMMS_ERROR, commh->stream,"[%s] Listener Thread could not be created!\n", __func__);
            commh->running = false;
            return false;
        }
    }
    char * ip = inet_ntoa(commh->local_address.asv4.sin_addr);
    uint16_t port = ntohs(commh->local_address.asv4.sin_port);
    CommsLog(COMMS_INFO, commh->stream,"[%s] Init UDP succesfull on %s:%u!\n", __func__, ip,port);
    return true;
}

bool comms_tcpserver_init__opt(commh_t *commh, comms_opt_t opt){
    
    if (!commh) {
        CommsLog(COMMS_ERROR, stderr,"[%s] : Context not provided!\n", __func__);
        return false;
    }
    commh->stream = stdout;

    int domain = AF_INET;

    if(opt.ipv6){
        TODO("ipV6 is not implemented yet");
        // domain = AF_INET6;
        // commh->local_address.asv6.sin6_family = AF_INET6;
    }else{  // IPV4
        commh->local_address.asv4.sin_family = AF_INET;

        if(opt.local_port != 0){
            commh->local_address.asv4.sin_port = htons(opt.local_port);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] Port added: %u!\n", __func__, opt.local_port);
        }else{   
            commh->local_address.asv4.sin_port = htons(COMMS_DEF_LISTEN_PORT);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] Port set as default: %u !\n ", __func__, COMMS_DEF_LISTEN_PORT);
        }

        if(opt.local_ip != NULL){
            commh->local_address.asv4.sin_addr.s_addr = inet_addr(opt.local_ip);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] IP added: %s!\n", __func__, opt.local_ip);
        }else{   
            commh->local_address.asv4.sin_addr.s_addr = inet_addr(COMMS_DEF_LISTEN_IP);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] IP set as default: %s!\n", __func__, COMMS_DEF_LISTEN_IP);
        }
    }

    int sock = socket(domain,SOCK_STREAM,0);
    if(sock < 0){
        _set_error(commh, COMMS_ERROR_SOCKET, __func__, __LINE__);
        CommsLog(COMMS_ERROR, commh->stream,"[%s] Socket could not be created!\n", __func__);
        return false;
    }
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    commh->clients_socks[0] = sock;
    int err = bind(commh->clients_socks[0], (const struct sockaddr*)&commh->local_address, (socklen_t) sizeof(struct sockaddr_in));

    if(err < 0 ){
        commh->clients_socks[0] = -1;
        _set_error(commh, COMMS_ERROR_BIND, __func__, __LINE__);
        CommsLog(COMMS_ERROR, commh->stream,"[%s] Binding could not be done on socket!\n", __func__);
        CommsClose(commh);
        return false;
    }

    err = listen(commh->clients_socks[0], 16);
    if(err < 0){
        _set_error(commh, COMMS_ERROR_LISTEN, __func__, __LINE__);
        CommsLog(COMMS_ERROR, commh->stream,"[%s] Listen could not be done on socket!\n", __func__);
        CommsClose(commh);
        return false;
    }

    commh->recvcb = opt.recvcb;
    commh->client_count = 0;
    commh->running = (commh->recvcb != NULL);
    commh->tcp = true;
    commh->server = true;
    commh->client_count = 1;

    if (commh->running) {
        
        int rc = pthread_create(&commh->listener, NULL, comms__listener, commh);
        if (rc != 0) {
            _set_error(commh, COMMS_ERROR_FAILED_TO_CREATE_LISTENER, __func__, __LINE__);
            CommsLog(COMMS_ERROR, commh->stream,"[%s] Listener Thread could not be created!\n", __func__);
            commh->running = false;
            return false;
        }
        rc = pthread_create(&commh->accepter, NULL, comms_accepter, commh);

        if (rc != 0) {
            _set_error(commh, COMMS_ERROR_FAILED_TO_CREATE_ACCEPTER, __func__, __LINE__);
            CommsLog(COMMS_ERROR, commh->stream,"[%s] Accepeter Thread could not be created!\n", __func__);
            commh->running = false;
            return false;
        }
    }
    char * ip = inet_ntoa(commh->local_address.asv4.sin_addr);
    uint16_t port = ntohs(commh->local_address.asv4.sin_port);
    CommsLog(COMMS_INFO, commh->stream,"[%s] Init TCP server succesfull on %s:%u!\n", __func__, ip,port);
    return true;  
}

bool comms_tcpclient_init__opt(commh_t *commh, comms_opt_t opt){
    if (!commh) {
        CommsLog(COMMS_ERROR, stderr,"[%s] : Context not provided!\n", __func__);
        return false;
    }
    int domain = AF_INET;
    commh->stream = stdout;

    if(opt.ipv6){
        TODO("ipV6 is not implemented yet");
        // domain = AF_INET6;
        // commh->local_address.asv6.sin6_family = AF_INET6;
    }else{  // IPV4
        commh->local_address.asv4.sin_family = AF_INET;

        if(opt.local_port != 0){
            commh->local_address.asv4.sin_port = htons(opt.local_port);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] Port added: %u!\n", __func__, opt.local_port);
        }else{   
            commh->local_address.asv4.sin_port = htons(COMMS_DEF_LISTEN_PORT);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] Port set as default: %u !\n ", __func__, COMMS_DEF_LISTEN_PORT);
        }

        if(opt.local_ip != NULL){
            commh->local_address.asv4.sin_addr.s_addr = inet_addr(opt.local_ip);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] IP added: %s!\n", __func__, opt.local_ip);
        }else{   
            commh->local_address.asv4.sin_addr.s_addr = inet_addr(COMMS_DEF_LISTEN_IP);
            CommsLog(COMMS_DEBUG, commh->stream,"[%s] IP set as default: %s!\n", __func__, COMMS_DEF_LISTEN_IP);
        }
    }

    int sock = socket(domain,SOCK_STREAM,0);
    if(sock < 0){
        _set_error(commh, COMMS_ERROR_SOCKET, __func__, __LINE__);
        CommsLog(COMMS_ERROR, commh->stream,"[%s] Socket could not be created!\n", __func__);
        return false;
    }
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    commh->clients_socks[0] = sock;
    int err = bind(commh->clients_socks[0], (const struct sockaddr*)&commh->local_address, (socklen_t) sizeof(struct sockaddr_in));

    if(err < 0 ){
        commh->clients_socks[0] = -1;
        _set_error(commh, COMMS_ERROR_BIND, __func__, __LINE__);
        CommsLog(COMMS_ERROR, commh->stream,"[%s] Binding could not be done on socket!\n", __func__);
        CommsClose(commh);
        return false;
    }

    commh->recvcb = opt.recvcb;
    commh->client_count = 0;
    commh->running = (commh->recvcb != NULL);
    commh->tcp = true;
    commh->server = false;

    if (commh->running) {
        
        int rc = pthread_create(&commh->listener, NULL, comms__listener, commh);
        if (rc != 0) {
            _set_error(commh, COMMS_ERROR_FAILED_TO_CREATE_LISTENER, __func__, __LINE__);
            CommsLog(COMMS_ERROR, commh->stream,"[%s] Listener Thread could not be created!\n", __func__);
            commh->running = false;
            return false;
        }
    }
    char * ip = inet_ntoa(commh->local_address.asv4.sin_addr);
    uint16_t port = ntohs(commh->local_address.asv4.sin_port);
    CommsLog(COMMS_INFO, commh->stream,"[%s] Init TCP client succesfull on %s:%u!\n", __func__, ip,port);

    return true;  
}

static void _set_error(commh_t *commh, uint8_t err, const char *f, int l){
    if (!commh) {
        CommsLog(COMMS_ERROR, stderr,"[%s] : Context not provided!\n", __func__);
        return;
    }
    commh->err_type = (comms_error_t)err;
    snprintf(commh->error, sizeof commh->error, "%s:%d", f, l);
}

#endif

#endif // ETH_COMMS_H_

