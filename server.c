#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#define WRKDIR_LEN	256
#define DEFDIR		"."

#define LIST	4 /* LIST dir/chdir */
#define CWD		8 /* CWD dir/chdir */
#define FTPER	16 /* flag of error */
#define FTPOK	32 /* success */

/*******************************************

1. Print IP and Port
2. GitHub
3. Shell script
4. Exploit

*******************************************/

typedef struct clThread {
	int sock;
	char wrkDir[WRKDIR_LEN];
	struct clThread* pnext;
}clThread, *pclThread;

struct requestHdr {
	int cmd; // request type
	int body_size; // size of raw data
};

static pthread_t* pThreads = NULL;
static int threads;
static pthread_mutex_t mCon;

static pclThread pclHead = NULL;
static int ClCounter = 0;

// epoll
static struct epoll_event *events;
static int hEvent;

// queue
static int pipedes[2];

int UnlockIo(int sfd)
{
  int flags, s;

  flags = fcntl (sfd, F_GETFL, 0);
  if (flags == -1){
      perror ("fcntl");
      return -1;
  }

  flags |= O_NONBLOCK;
  s = fcntl (sfd, F_SETFL, flags);
  if (s == -1){
      perror ("fcntl");
      return -1;
  }

  return 0;
}

int CreateListener(int port)
{
    int s;
    struct sockaddr_in addr;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0){
        perror("socket");
        return -1;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        perror("bind");
        return -1;
    }

    return s;
}

void CloseSession(pclThread p)
{
	pclThread tmp, prev = NULL;
	printf("close connection. sock %d \n", p->sock);
	close(p->sock);
	p->sock = 0;
	memset(p->wrkDir, 0, WRKDIR_LEN);
	// delete link
	pthread_mutex_lock(&mCon);
	tmp = pclHead;
	while(tmp != NULL){
		if(tmp == p){
			if(prev)
				prev->pnext = tmp->pnext;
			else // head == p
				pclHead = p->pnext;
				
			break;
		}
		prev = tmp;
		tmp = tmp->pnext;
	}
	pthread_mutex_unlock(&mCon);
	free(p);
}

char* BuildPacket(char* szDir)
{
	int len = 0;
	char path[1024];
	char* szString = NULL, *szBody = NULL;
	struct dirent* dp = NULL;
	
	DIR* dirp = opendir(szDir);
	if(dirp == NULL) return NULL;
    while ((dp = readdir(dirp)) != NULL)
		if (dp->d_name[0] != '.')
			len += strlen(szDir)+strlen(dp->d_name)+5;
	closedir(dirp);
	if(len == 0) return NULL;
	if((dirp = opendir(szDir)) == NULL) return NULL;
	szString = (char*)malloc(len+sizeof(struct requestHdr));
	if(szString==NULL) return NULL;
	memset(szString, 0, len);
	szBody = szString + sizeof(struct requestHdr);

	while ((dp = readdir(dirp)) != NULL){
		if (dp->d_name[0] != '.') {
			path[0] = 0;
			strcpy(path, szDir);
			strcat(path, "/");
			strcat(path, dp->d_name);
			strcat(path, "\n");
			strcat(szBody, path);
      }
	}
	
	closedir(dirp);
	return szString;
}

void RespondError(int s)
{
	struct requestHdr rhdr;
	rhdr.cmd = FTPER;
	rhdr.body_size = 0;
	send(s, (char*)&rhdr, sizeof(struct requestHdr), 0);
}

void* Client_Thread(void* argv)
{
	pclThread thread = NULL;
	char buf[512];
	//char* buf = malloc(300);
	int len;
	struct requestHdr rhdr; // header of the request
	char *pPacket;
	
	for(;;){
		memset(buf,0, sizeof buf);
		memset(&rhdr, 0, sizeof(struct requestHdr));
		read(pipedes[0], &thread, sizeof(pclThread));
		/************ read header *****************/
		len = read(thread->sock, (char*)&rhdr, sizeof(struct requestHdr));
		if(len>0 && len>=sizeof(struct requestHdr)){ // If data present, I must read their
		/*********** read body ***********************/
		printf("HEADER(sock %d): header_size: %d rhdr.body_size: %d\n",
									thread->sock, len, rhdr.body_size);
			if(rhdr.body_size) len = read(thread->sock, buf, rhdr.body_size);
			if(len>0 && len>=rhdr.body_size) { // full data
				if(rhdr.cmd & LIST){
					printf("LIST:wrkDir = %s\n", thread->wrkDir);
					pPacket = BuildPacket(thread->wrkDir);
					if(pPacket != NULL){ // success
						// I must calculate size of respond packet!!
						rhdr.cmd = FTPOK;
						rhdr.body_size = strlen(pPacket+sizeof(struct requestHdr))+1;
						memcpy(pPacket,&rhdr,sizeof(struct requestHdr));
						printf("body_size: %d\n", rhdr.body_size);
						printf("sock %d LIST: %s\n%s\n", thread->sock,
								thread->wrkDir,
								(char*)(pPacket+sizeof(struct requestHdr)));
						write(thread->sock, pPacket, rhdr.body_size+sizeof(struct requestHdr));
						free(pPacket);
					} else { // error
						printf("sock %d LIST szDir==0\n", thread->sock);
						RespondError(thread->sock);
					}
				} else if(rhdr.cmd & CWD){
					strcpy(thread->wrkDir, buf);
					printf("sock %d CWD: %s\n", thread->sock, thread->wrkDir);
					//rhdr.cmd |= FTPOK;
					//rhdr.body_size = 0;
					//send(thread->sock, (char*)&rhdr, sizeof(struct requestHdr), 0);
				}
			} else if(len == 0) {
				printf("![read_body]connection was closed\n");
				CloseSession(thread); // close sock
			} else if(len == -1){ // may be part of data was read
				printf("I can't read all data from package\n");
				//RespondError(thread->sock);
			} else {printf("body_unknown\n");}
		} else if(len==0) {
			printf("![read_header]connection was closed\n");
			CloseSession(thread); // close sock
		} else if(len == -1){
			printf("!error [read_header]\n");
			//RespondError(thread->sock);
		} else {printf("header_unknown\n");}
	}
	
	return 0;
}

void* Scheduler(void* argv)
{
	int i,n;
	for(;;)
    {
		n = epoll_wait (hEvent, events, threads, -1);
		
		for (i = 0; i < n; i++)
		{
			if ((events[i].events&EPOLLERR)||
				(events[i].events&EPOLLHUP)||
				(events[i].events&EPOLLRDHUP)||
				(!(events[i].events&EPOLLIN)))
			{
				printf("epoll error or close socket\n");
				CloseSession(((struct clThread*)events[i].data.ptr));
				continue;
			} else {
				printf("incoming data\n");
				write(pipedes[1], &events[i].data.u32, 4);
			}
		}
	}
        
	return 0;
}

int main(int argc, char *argv[])
{
    int sock=0, listener, i, listen_port, t=sizeof(struct sockaddr); 
	pthread_t hScheduler;
	struct epoll_event event;
	pclThread pnewClient = NULL;
	struct sockaddr_in sock_inf;
	pthread_mutex_init(&mCon, 0);
	char* szip;

	if(argc != 3){
		printf("argc != 3\n");
		return -1;
	}
	
	if(pipe(pipedes)!=0){
		perror("pipe");
		return 1;
	}
	
	threads = atoi(argv[2]);
	listen_port = atoi(argv[1]);
    
    // work threads
    pThreads = (pthread_t*)malloc(sizeof(pthread_t)*threads);
	if(pThreads == NULL){
		perror("malloc: for threads\n");
		return -1;
	}
	
	hEvent = epoll_create(threads);
	if (hEvent == -1){
		perror ("epoll_create");
		abort ();
	}
	
	events = (struct epoll_event*)malloc(threads*sizeof(struct epoll_event));
	if(events == NULL){
		perror("malloc\n");
		return -1;
	}
    
    // start scheduler
    pthread_create(&hScheduler, NULL, Scheduler, 0);
    printf("port = %d\nthreads = %d\n", listen_port, threads);
	
	// start client threads
	for(i=0; i<threads; i++)
		pthread_create((pthread_t*)(pThreads+sizeof(pthread_t)*i), NULL, Client_Thread, NULL/*argv*/);
    
    //
    listener = CreateListener(listen_port);
    if(listener == -1) return -1;
	listen(listener, threads/*countclients*/);
    
    for(;;)
    {
        sock = accept(listener, &sock_inf, &t);
        szip = inet_ntoa(sock_inf.sin_addr);
        printf("New client port %d ip %s sock %d\n", sock_inf.sin_port,szip,sock);
					
        if(sock < 0){
            perror("accept");
            exit(3);
        }
        
		pnewClient = (pclThread)malloc(sizeof(clThread));
		UnlockIo(sock);
		pnewClient->sock = sock;
		strcpy(pnewClient->wrkDir, DEFDIR);
		pnewClient->pnext = NULL;
		
		// save link 
		pthread_mutex_lock(&mCon);
		ClCounter++;
		if(pclHead){
			pnewClient->pnext = pclHead;
			pclHead = pnewClient;
		} else {
			pclHead = pnewClient;
		}
		pthread_mutex_unlock(&mCon);
		// add to epoll
		event.data.ptr = pnewClient;
		event.events = EPOLLIN | EPOLLET;
		if (epoll_ctl(hEvent, EPOLL_CTL_ADD, sock, &event) == -1){
			perror ("epoll_ctl");
			break;
		}
	}
    
    close(listener);
    return 0;
}
