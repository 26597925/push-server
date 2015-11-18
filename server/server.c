/******************************************************************************
  Copyright (c) 2015 by Baida.zhang(Tong.zhang)  - zt9788@126.com

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
******************************************************************************/

#include "pushserver.h"
#include "sc.h"
#include "messagestorage.h"
#include "log.h"
#include "md5.h"
//#include "uuid32.h"
#include "readconf.h"

#include "util.h"
#include "thread_pool.h"

#include "daemon.h"
#include <sys/resource.h>
#include <syslog.h>
#include <time.h>
#include "parse_command.h"
#ifdef CHECKMEM
#include "leak_detector_c.h"
#endif
#include "socket_plush.h"

#ifdef DEBUG
//#undef Log
//#define Log(format,...) NULL
//#else
#define DEBUGFMT  "File:%s(%d)-%s:"
#define DEBUGARGS __FILE__,__LINE__,__FUNCTION__
//#define Log(format, ...) Log (DEBUGFMT format, ##__VA_ARGS__)
#define __DEBUG(format, ...) printf("FILE: "__FILE__", LINE: %d: "format"/n", __LINE__, ##__VA_ARGS__)
#define _freeReplyObject(rd)\
	printf("FILE: "__FILE__", LINE: %d: \n /n",__LINE__ ); \
	freeReplyObject(rd);

// printf (DEBUGFMT format,DEBUGARGS, ##__VA_ARGS__)
//#define __DEBUG( ) printf (DEBUGFMT,DEBUGARGS)
#endif


#define SEM_NAME "mysem"
#define OPEN_FLAG O_RDWR|O_CREAT
#define OPEN_MODE 00777
#define INIT_V    1


static struct config_struct system_config;

static pthread_mutex_t lock;

//static sem_t **sem = NULL;
static pid_t pid = -1;
pid_t main_pid = -1;
//static volatile sig_atomic_t _running = 1;
volatile int g_isExit = 0;
volatile int g_isRestart = 0;
#ifndef __EPOLL__
unsigned int lisnum = FD_SETSIZE;
CLIENT g_clients[FD_SETSIZE];

#else
unsigned int lisnum = MAX_CONNECTION_LENGTH;
CLIENT g_clients[MAX_CONNECTION_LENGTH];
static struct epoll_event eventList[MAX_EVENTS];
#endif
static pthread_mutex_t client_lock;
pthread_t* coretthreadsId;
int active_socket_connect = 0;
int g_maxi = -1;
static int g_epollfd;
fd_set rset,allset;


void* read_thread_function(void* client_t);
void* unionPushList(void* st);
void* pushlist_send_thread(void* st);//thread_function
/*
//信号捕捉处理
static void myhandler(char* name,sem_t* semt)
{
    //删掉在系统创建的信号量
    sem_unlink(name);
    //彻底销毁打开的信号量
    sem_close(semt);
}
*/
int addepollevent(int fd,void* ptr)
{
#ifdef __EPOLL__
    struct epoll_event event;
    if(ptr == NULL)
        event.data.fd = fd;
    else
        event.data.ptr = ptr;
    event.events =  EPOLLIN|EPOLLET;  //EPOLLET
    if(epoll_ctl(g_epollfd, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        Log("epoll add fail : fd = %d\n", fd);
        return -1;
    }
#endif
    return 0;
}

int mksock(int type)
{
    int sock;
    struct sockaddr_in  my_addr;
    if((sock = socket(AF_INET,type,0)) == -1)
    {
        perror("socket");
        exit(1);
    }

    //bzero(&my_addr,sizeof(my_addr));
    memset(&my_addr,0,sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(system_config.server_port);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    //close socket just now
    unsigned int value = 0x1;
    setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(void *)&value,sizeof(value));
    return sock;

}

/***************************
**server for multi-client
**PF_SETSIZE=1024
****************************/
//#define FD_SETSIZE 1024
int main(int argc, char** argv)
{

    int i,n;
    int nready;
    //int autoclientid = 1;
    int slisten,sockfd,maxfd=-1;//,connectfd;

    //unsigned int lisnum;

    struct sockaddr_in  my_addr;//,addr;
    struct timeval tv;

    char buf[MAXBUF + 1];

    if(argc < 2)
    {
        Log("Usage: %s configfile",argv[0]);
        exit(0);
    }
    read_config(argv[1],&system_config);
    print_welcome();
    if(strcasecmp(system_config.deamon,"yes") == 0)
    {
        init_daemon(argv[1]);
    }

    slisten = mksock(SOCK_STREAM);
    memset(&my_addr,0,sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(system_config.server_port);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    pthread_mutex_init(&client_lock, NULL);
    if(bind(slisten, (struct sockaddr *)&my_addr, sizeof(my_addr)) == -1)
    {
        perror("bind");
        exit(1);
    }

    if (listen(slisten, lisnum) == -1)
    {
        perror("listen");
        exit(1);
    }
    setnonblocking(slisten);
    pthread_mutex_lock(&client_lock);
#ifdef __EPOLL__
    for(i=0; i<MAX_CONNECTION_LENGTH; i++)
#endif
    {
        g_clients[i].fd = -1;
        g_clients[i].clientId = i;
        g_clients[i].serverid = system_config.serverid;
        memset(g_clients[i].drivceId,0,64);
    }
    pthread_mutex_unlock(&client_lock);

    FD_ZERO(&allset);
    FD_SET(slisten, &allset);
    maxfd = slisten;

    Log("Waiting for connections and data...\n");

    //start push quene
    struct pushstruct st;
    st.client = g_clients;
    st.timeout=system_config.push_time_out;

    struct pushstruct st2;
    memcpy(&st2,&st,sizeof(pushstruct));
    st2.timeout=system_config.delay_time_out;

    createRedisPool(system_config.redis_ip, system_config.redis_port,30);
    int redisID= -1;
    redisContext* redis = getRedis(&redisID);
    tpool_create(system_config.max_recv_thread_pool);
    pthread_mutex_init(&st.lock, NULL);
    pthread_mutex_lock(&st.lock);
    st.isexit = 0;
    int ij=0;
    coretthreadsId = malloc(system_config.push_thread_count+system_config.unin_thread_count);
    for(ij=0; ij<system_config.push_thread_count; ij++)
    {
        //tpool_add_work(pushlist_send_thread,(void*)&st);
        pthread_create(&coretthreadsId[ij],NULL,pushlist_send_thread,(void*)&st);
    }
    pthread_mutex_unlock(&st.lock);
    pthread_mutex_init(&st2.lock, NULL);
    pthread_mutex_lock(&st2.lock);
    st2.isexit = 0;
    //tpool_add_work(unionPushList,(void*)&st2);
    pthread_create(&coretthreadsId[ij],NULL,unionPushList,(void*)&st2);
    pthread_mutex_unlock(&st2.lock);
    //end

    //create processer

    int processercount = system_config.processer;
    //pid_t* fid = malloc(sizeof(pid_t)*processercount);
    //sem = malloc(sizeof(sem_t)* processercount);
    char tempsemStr[10];
    itoa_(0,tempsemStr);
    //sem[0] = sem_open(tempsemStr, OPEN_FLAG, OPEN_MODE, INIT_V);
    main_pid = getpid();
    /*************************************
    //多fork 一个要解决惊群问题，还有一个就是client数组轮训的时候，不知道是那个
    //fork接收的数据
    for(ij=0; ij<processercount; ij++)
    {
    	fid[ij] = fork();
    	if(fid[ij]<0)
    	{
    		printf("create process error");
    	}
    	//sun process
    	if(fid[ij] == 0)
    	{
    		pid = getpid();//fid[ij];
    		itoa_(pid,tempsemStr);
    		sem[ij+1] = sem_open(tempsemStr, OPEN_FLAG, OPEN_MODE, INIT_V);
    		break;
    	}
    	//father process
    	else
    	{
    		pid = getpid();//fid[ij];
    	}
    }

    ********************************************/


    Log("this process's pid=%d , and main pid is %d\n",pid,main_pid);
#ifdef __EPOLL__
    //1. epoll 初始化
    int epollfd = epoll_create(MAX_EVENTS);
    g_epollfd = epollfd;
    if(addepollevent(slisten,NULL)<0)
    {
        Log("epoll add fail : fd = %d\n", slisten);
        return -1;
    }
    //EPOLL相关
    //事件数组
    if(pid != 0)
    {
        addepollevent(0,NULL);
    }

#endif

    while (g_isExit == 0 && g_isRestart == 0)//sig = xxx wait for sig
    {
        if(pid == main_pid)
        {

        }
        pthread_mutex_lock(&client_lock);
        int timeout = 5;
#ifdef __EPOLL__

        nready = epoll_wait(epollfd, eventList, MAX_EVENTS, timeout);
#endif
        pthread_mutex_unlock(&client_lock);
        int isQuit = 0;
        if(nready == 0)
        {
            continue;
        }
        else if(nready < 0)
        {
            printf("select failed!\n");
            break;
        }
        else
        {
#ifdef __EPOLL__
            int n = 0;

            for(n=0; n<nready; n++)
            {
                //错误退出
                if ((eventList[n].events & EPOLLERR) ||
                        (eventList[n].events & EPOLLHUP) ||
                        !(eventList[n].events & EPOLLIN))
                {
                    Log ( "epoll error\n");
                    CLIENT* clientx = (CLIENT*)eventList[n].data.ptr;
                    if(eventList[n].data.ptr < 0) continue;
                    //int sockfd = -1;
                    if((sockfd = clientx->fd)<0)
                        continue;
                    removeClient(clientx);//TODO here some time client is not have drivceid may make a mistake
                    Log("eventList error！errno:%d，error msg: '%s'\n", errno, strerror(errno));
                    //epoll_ctl(epollfd,  EPOLL_CTL_DEL, sockfd, NULL);
                    close_socket(clientx,&allset);
//					return -1;
                    continue;
                }
                else  if (eventList[n].data.fd == slisten)
                {
                    int ret = new_connection(slisten,g_clients,&allset,&maxfd,epollfd);
                    if(ret != 1)
                        continue;
                }
                else if (eventList[n].data.fd == 0)
                {
                    if(pid != 0)
                    {
                        printf("pushserver> ");
                        fflush(stdout);
                        char btf[MAXBUF];
                        fgets(btf, MAXBUF, stdin);
                        if (!strncasecmp(btf, "quit", 4))
                        {
                            Log("request terminal chat！\n");
                            isQuit = 1;
                            break;
                        }
                    }
                }
                else
                {

                    CLIENT* clientx = (CLIENT*)eventList[n].data.ptr;
                    if(eventList[n].data.ptr < 0) continue;
                    if((sockfd = clientx->fd)<0)
                        continue;
                    int lockret = pthread_mutex_trylock(&clientx->opt_lock);
                    if(lockret == EBUSY)
                    {
                        __DEBUG("client id continue:%d,%s\n",clientx->clientId,clientx->drivceId);
                        continue;
                    }
                    tpool_add_work(read_thread_function,(void*)clientx);
                    //read_thread_function((void*)clientx);
                }
                if(isQuit == 1)
                {
                    break;
                }
            }
#endif
        }
        if(isQuit == 1)
        {
            break;
        }

    }
    pthread_mutex_init(&st.lock, NULL);
    st.isexit = 1;
    pthread_mutex_unlock(&st.lock);
    pthread_mutex_init(&st2.lock, NULL);
    st2.isexit = 1;
    pthread_mutex_unlock(&st2.lock);
    for(ij=0; ij<(system_config.push_thread_count+system_config.unin_thread_count); ij++)
    {
        pthread_join(coretthreadsId[ij],NULL);
    }

#ifdef __EPOLL__
    close(epollfd);
#endif
    free(coretthreadsId);
    tpool_destroy();
    destroy_redis_pool();
    close(slisten);
    char pragme[500];
    getRealPath(pragme);
    strcat(pragme,"/");
    strcat(pragme,"pushserver.lock");
    deletePidFile(pragme);
#ifdef CHECKMEM
    atexit(report_mem_leak);
#endif
}


void* sendMessage(void* args)
{
    send_info_t* sendinfo = (send_info_t*)args;
    if(sendinfo == NULL)
        return NULL;
    pthread_mutex_lock(&sendinfo->client->opt_lock);
    int ret = send_pushlist_message(sendinfo->info,sendinfo->client);
    if(sendinfo->info)
    {
        freePushMessage(sendinfo->info);
    }
    if(ret<=0)
    {
        saveInNoread(sendinfo->client->drivceId,sendinfo->info->messageid);
    }
    pthread_mutex_unlock(&sendinfo->client->opt_lock);
    return NULL;
}
void* send_helo_to_client_message(CLIENT* client)
{
    char* token = client->drivceId;
    char pushid[255]= {0};
    int ret = 0;
    do
    {
        ret = getNextNoReadMessageId(client->drivceId,pushid);
        if(ret <= 0)
            break;
        push_message_info_t* messageinfo = NULL;
        getmessageinfo(pushid,&messageinfo);
        if(messageinfo == NULL)
//				goto next_while;
            continue;

        int lockret = pthread_mutex_lock(&client->opt_lock);
        //if(lockret == EBUSY)
        //	continue;
        int isHasClient = send_pushlist_message(messageinfo,client);
        freePushMessage(messageinfo);
        pthread_mutex_unlock(&client->opt_lock);
        //Log("isHasClient=%d,errno:%d,error msg: '%s'\n",isHasClient, errno, strerror(errno));
        if(isHasClient > 0)
        {

        }
    }
    while(ret>0);
}
/*
void send_helo(int sockfd,CLIENT_HEADER	*header,CLIENT* client,char* deviceid)
{
    char time[255];
    formattime(time,NULL);
    struct sockaddr_in addr;
    socklen_t slen;
    getsockname(sockfd,(struct sockaddr*)&addr,&slen);
    strcpy(client->drivceId,deviceid);
    client_info_t* clientinfo = NULL;
    newClient(client,system_config.serverid,inet_ntoa(addr.sin_addr),
              client->drivceId,
              1, 
              &clientinfo);
    freeClientInfo(clientinfo);
    /////////////////////////////////////////////////////////////////
    send_OK(client->fd,MESSAGE_HELO);

    send_helo_to_client_message(client);
}
*/

void close_socket(CLIENT* client,fd_set* allset)
{
    if(active_socket_connect<1)
        return;
    //CLIENT* clientx = &client[clientId];
    Log("%s:%d disconnected by client!\n",inet_ntoa(client->addr.sin_addr),client->addr.sin_port);
    int sockfd = client->fd;
    int clientid = client->clientId;
    //TODO需要重新排序
    if(active_socket_connect>=2)
    {
        pthread_mutex_lock(&client_lock);

#ifdef __EPOLL__
        epoll_ctl(g_epollfd,  EPOLL_CTL_DEL, client->fd, NULL);
        client->fd = -1;
        memset(client->drivceId,0,sizeof(client->drivceId));
        active_socket_connect--;
#endif
        pthread_mutex_unlock(&client_lock);
    }
    else if(active_socket_connect == 1)
    {
        pthread_mutex_lock(&client_lock);
        active_socket_connect--;
        //g_maxi--;

#ifdef __EPOLL__
        epoll_ctl(g_epollfd,  EPOLL_CTL_DEL, client->fd, NULL);
#endif
        client->fd = -1;
        pthread_mutex_unlock(&client_lock);
    }

    close(sockfd);
    FD_CLR(sockfd,allset);


}
/*****************************
 *
 *list pushlist
  {
  	messageid(guid)
  }
 *
 *list device_noread ( recv device list)
  {
  	messageid(guid)
  }
 *
 *hash messageid(guid)
  {
  	from:from device
  	to:  recv device
  	fromip:from client ip
  	state:0(no read),1(read)
  	content:messages
  	sendtime:send time
  	retrycount:0(try to send count)
  	messagetype:0xfxx
  	EXPIRE:120(TODO)
  }
 *
 *
 ***********************/
void send_OK(int sockfd,int type)
{
    SERVER_HEADER* sheader = malloc(sizeof(SERVER_HEADER));
    sheader->messageCode = MESSAGE_SUCCESS;
    if(type != -1)
        sheader->type = type;
    else
        sheader->type = S_OK;
    sheader->messagetype = MESSAGE_TYPE_TXT;
    sheader->totalLength = sizeof(SERVER_HEADER);
    int len = send(sockfd, sheader, sizeof(SERVER_HEADER) , 0);
    //Log("len=%d:%d ,errno:%d，error msg: '%s'\n",len,sizeof(SERVER_HEADER) , errno, strerror(errno));

    free(sheader);
}
int new_connection(int slisten,//struct sockaddr_in* addr,
                   CLIENT* client,fd_set* allset,
                   int* maxfd,int epollfd)
{
    socklen_t len;
    //fd_set rset,allset;
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(struct sockaddr_in));
    len = sizeof(struct sockaddr);
    int connectfd;
    if((connectfd = accept(slisten,
                           (struct sockaddr*)&addr,&len)) == -1)
    {
        perror("accept() error\n");
        return 0;
    }
    setnonblocking(connectfd);
    struct timeval timeout =
    {
        1,0
    };
    setsockopt(connectfd,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof(struct timeval));
//    	setsockopt(connectfd,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(struct timeval));
    int i = 0;
    pthread_mutex_lock(&client_lock);
    for(i=0; i<MAX_CONNECTION_LENGTH; i++)
    {
        if(client[i].fd < 0)
        {
            client[i].fd = connectfd;
            //client[i].clientid=autoclientid;
            client[i].addr = addr;
            pthread_mutex_init(&client[i].opt_lock , NULL);
            Log("Yout got a connection from %s.\n",
                inet_ntoa(client[i].addr.sin_addr));
            active_socket_connect++;
            Log("now connection user is %d\n",active_socket_connect);
            //autoclientid++;
            break;
        }
    }
    pthread_mutex_unlock(&client_lock);

    if(i == MAX_CONNECTION_LENGTH)
        Log("too many connections");
    pthread_mutex_lock(&client_lock);
#ifdef __EPOLL__
    addepollevent(connectfd,&client[i]);
#endif
    if(connectfd > *maxfd)
        *maxfd = connectfd;
    if(i > g_maxi)
        g_maxi = i;
    pthread_mutex_unlock(&client_lock);
    return 1;

}


void* read_thread_function(void* client_t)
{
    char buf[MAXBUF];// = malloc(MAXBUF);
    int sockfd;
    CLIENT* client = (CLIENT*)client_t;
    sockfd = client->fd;
    int n  = -1;
    memset(buf,0,MAXBUF + 1);
    int reads = 1;
    int epollfd = g_epollfd;
    if((n = recv(sockfd,buf,MAXBUF,0)) > 0)
    {
        //TODO Judging communication protocol, custom,websocket,xmpp or SSL
        //if (strlen(buf) < 14) {
        //  it is may SSL
        //	Log("SSL request is not supported yet.\n");
        //}
        if(strncasecmp(buf,"get",3) == 0)
        {
            //TODO it is may websocket

        }
        else if(strncasecmp(buf,"<",1) == 0)
        {
            //TODO it is may xmpp
        }
        dump_data(buf,n);
        client_header_2_t header;
        memset(&header,0,sizeof(client_header_2_t));
        void* bufs = parseClientHeader((void*)buf,&header);
        //if(header == NULL)
        //	goto recv_error_close_client;
        n -= sizeof(client_header_2_t);
        if(header.command == COMMAND_PING)
        {
            server_header_2_t* sheader = createServerPing(system_config.serverid);
            int ret = send(sockfd,sheader,sizeof(server_header_2_t),0);
            free(sheader);
            if(ret <= 0)
                goto recv_error_close_client;
        }
        else if(header.command == COMMAND_HELO)
        {
            int ret = parseClientHelo(bufs,client->drivceId,client->token,header.messagetype);
            struct sockaddr_in addr;
            socklen_t slen;
            getsockname(sockfd,(struct sockaddr*)&addr,&slen);
            client_info_t* clientinfo = NULL;
            newClient(client,system_config.serverid,inet_ntoa(addr.sin_addr),
                      client->drivceId,
                      1, /* TODO is need get from client header from client */
                      &clientinfo);

            if(ret < 1)
                goto recv_error_close_client;
            void* sheader = createServerHelo(system_config.serverid,0,0,0);
            ret = send(sockfd,sheader,sizeof(server_header_2_t),0);
            free(sheader);
            if(ret <= 0)
                goto recv_error_close_client;
                
            send_helo_to_client_message(client);
        }
        else if(header.command == COMMAND_BYE)
        {
            goto recv_error_close_client;
        }
        else if(header.command == COMMAND_MESSAGE)
        {
            int ret = parseClientMessage(sockfd,bufs,&header,client->drivceId,client->token,n,
                                         inet_ntoa(client->addr.sin_addr),system_config.tempPath);
            if(ret < 0)
                goto recv_error_close_client;
            server_header_2_t* sheader = createServerHeader(system_config.serverid,COMMAND_YES,MESSAGE_TYPE_NULL);
            ret = send(sockfd,sheader,sizeof(server_header_2_t),0);
            if(ret <= 0)
                goto recv_error_close_client;
        }
        else if(header.command == COMMAND_OTHER_MESSAGE)
        {
            //...
        }
        else if(header.command == COMMAND_SERVER)
        {
            //...
        }
        else
        {
            Log("client say unkown code:%d ,%s,len:%d,recv:%s\n",header.messagetype,
                inet_ntoa(client->addr.sin_addr),n,buf);
recv_error_close_client:
            pthread_mutex_unlock(&client->opt_lock);
            removeClient(client);
            close_socket(client,&allset);
            return NULL;
        }
    }
    else
    {
        pthread_mutex_unlock(&client->opt_lock);
        Log("recv data< 0 errno:%d，error msg: '%s'\n", errno, strerror(errno));
        removeClient(client);
        close_socket(client,&allset);
    }
    pthread_mutex_unlock(&client->opt_lock);
    return NULL;
}

void* pushlist_send_thread(void* stx)
{
    pushstruct* st = (pushstruct*)stx;
    Log("Connect to redisServer Success\n");
    while(st->isexit == 0)
    {
        char messageid[255] = {0} ;
        int ret = 0;
        do
        {
            int ret = getNextMessageId(system_config.serverid,messageid);
            if(ret <= 0)
                break;
            int mm=0;
            int isHasClient = 0;

            push_message_info_t* messageinfo = NULL;
            getmessageinfo(messageid,&messageinfo);
            if(messageinfo == NULL)
            {
                //goto pop_pushlist;
                continue;
            }
            client_info_t* clientinfo = NULL;
            getClientInfo(&clientinfo,messageinfo->to);
            //print_client_info(clientinfo);
            if(clientinfo == NULL)
            {
                freePushMessage(messageinfo);
                goto save_dely_push;
            }
            if(messageinfo == NULL)
            {
save_dely_push:
                saveInNoread(messageinfo->to,messageid);
pop_pushlist:
                continue;
            }
            CLIENT* clientx = &st->client[clientinfo->clientid];
            if(clientx->fd != -1)
            {
                //TODO need to create a thread to send message
                //isHasClient = send_pushlist_message(messageinfo,clientx);
                send_info_t sendinfo;
                sendinfo.info = messageinfo;
                sendinfo.client = clientx;
                tpool_add_work(sendMessage,&sendinfo);
            }
            else
            {
                goto save_dely_push;
            }
            //delete to Improve the system performance
            freeClientInfo(clientinfo);
            if(st->isexit != 0)
            {
                Log("------------pushlist_send_thread thread exit------------------\n");
                goto exit_send_funtion;
            }
        }
        while(ret>0);
        int ij = 0;
        for(ij=0; ij<st->timeout; ij++)
        {
            if(st->isexit != 0)
            {
                Log("------------pushlist_send_thread thread exit------------------\n");
                goto exit_send_funtion;
            }
            sleep(1);
        }

    }
exit_send_funtion:
    Log("------------pushlist_send_thread thread exit------------------\n");
}

int send_pushlist_message(
    push_message_info_t* messageinfo,
    CLIENT* client)
{
    int ret = 0;
    int sendlen = sizeof(SERVER_HEADER);//+content!=NULL?strlen(content):0;
    SERVER_HEADER* sheader = malloc(sizeof(SERVER_HEADER));
    sheader->messageCode = MESSAGE_SUCCESS;
    sheader->type = MESSAGE;
    sheader->messagetype = messageinfo->messagetype;

    if(messageinfo->content!=NULL && messageinfo->messagetype == MESSAGE_TYPE_TXT)
    {
        sendlen += strlen(messageinfo->content);
        sheader->contentLength = strlen(messageinfo->content);
    }
    else if( messageinfo->content!=NULL && messageinfo->messagetype != MESSAGE_TYPE_TXT)
    {
        long filesize = get_file_size(messageinfo->content);
        sendlen += 255;
        sendlen += filesize;
        sheader->contentLength = (int)filesize;
    }
    else if(messageinfo->content == NULL)
        sheader->contentLength = 0;
    sheader->totalLength = sendlen;
    //TODO
    void* sendbuffer = malloc(sendlen);
    memset(sendbuffer,0,sendlen);
    memcpy(sendbuffer,sheader,sizeof(SERVER_HEADER));
    if(messageinfo->content != NULL)
    {
        if(messageinfo->messagetype != MESSAGE_TYPE_TXT)
        {
            memcpy(sendbuffer+sizeof(SERVER_HEADER),messageinfo->orgFileName,255);
            int ret;
            unsigned char* filecontent = readtofile(system_config.tempPath,messageinfo->content,&ret);
            memcpy(sendbuffer+sizeof(SERVER_HEADER)+255,filecontent,ret);

            free(filecontent);
        }
        else
        {
            memcpy(sendbuffer+sizeof(SERVER_HEADER),messageinfo->content,strlen(messageinfo->content));
        }

    }
    int len = -1;
    if(sendlen>MAXBUF)
    {
        int sumlen = 0;
        int i = 0;
        while(sumlen < sendlen)
        {
            int retsend = 0;
            if((sendlen-sumlen)>MAXBUF)
                retsend = send(client->fd, sendbuffer+sumlen,MAXBUF,0);
            else
                retsend = send(client->fd, sendbuffer+sumlen,(sendlen-sumlen),0);
            if(retsend<0)
            {
                goto send_error;
            }
            sumlen += retsend;
            Log("send data length=%d\n",sumlen);
            i++;
        }
        len = sumlen;
    }
    else
    {
        len = send(client->fd, sendbuffer, sendlen, 0);
    }
    Log("Send message:%s to %s len:%d,errno:%d,error msg: '%s'\n",messageinfo->content,client->drivceId,len, errno, strerror(errno));
    free(sendbuffer);
    sendbuffer = NULL;
    free(sheader);
    sheader = NULL;
    //TODO
    if(len < 0)
    {
send_error:
        removeClient(client);
        Log("send error and say goodbye '%s'\n", inet_ntoa(client->addr.sin_addr));
        //close_socket(client->fd,g_clients,client->clientId,&allset,g_epollfd);
        close_socket(client,&allset);
        return 0;
    }
    //wait for client return
    char tempbuf[MAXBUF];
    len = recv(client->fd, tempbuf, MAXBUF, 0);
    if(len>0)
    {
        CLIENT_HEADER* cheader = malloc(sizeof(CLIENT_HEADER));
        memset(cheader,0,sizeof(CLIENT_HEADER));
        memcpy(cheader,tempbuf,sizeof(CLIENT_HEADER));
        if(cheader->type == S_OK)
        {
            ret = 1;
            messageIsSendOK2( messageinfo,system_config.serverid);
        }

        free(cheader);
    }
    else
    {
        ret = 0;
        return ret;
    }
    return ret;
}
//
void* unionPushList(void* stx)
{
    struct pushstruct* stn;
    stn = (struct pushstruct*)stx;
    Log("Connect to redisServer Success\n");
    while(1)
    {
        char messageid[255] = {0};
        char drivceid[255] = {0};
        int ret = 0;
        do
        {
            ret = getDelyMessage(messageid);//TODO this method has bug!
            if(ret > 0 )
            {
                int ret2 = getClientDrivceIdFromMessage(messageid,drivceid);
                if(ret2 <= 0)
                    continue;
                client_info_t* clientinfo = NULL;
                //ret2 = isClientOnline(drivceid)
                getClientInfo(&clientinfo,drivceid);
                if(clientinfo != NULL)
                {
                    //for cluster
                    saveInPushlist(messageid,clientinfo->serverid);
                }
                else
                {
                    saveInNoread(drivceid,messageid);
                }
                freeClientInfo(clientinfo);
            }
        }
        while(ret > 0);
        /*
        redisReply* reply = (redisReply*)redisCommand(redis, "RPOPLPUSH dely_pushlist pushlist");
        while(reply->str != NULL)
        {
            _freeReplyObject(reply);
            if(stn->isexit != 0)
            {
                Log("------------union thread exit------------------\n");
                returnRedis(redisID);
                return NULL;
            }
            reply = (redisReply*)redisCommand(redis, "RPOPLPUSH dely_pushlist pushlist");
        }
        _freeReplyObject(reply);
        */
        int ij = 0;
        for(ij=0; ij<stn->timeout; ij++)
        {
            if(stn->isexit != 0)
            {
                Log("------------union thread exit------------------\n");
                return NULL;
            }
            sleep(1);
        }
    }
}
