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

char message[] = "Hello there!\n";
char buf[4096];

#define WRKDIR_LEN	256
#define DEFDIR		"."

#define LIST	4 /* LIST dir/chdir */
#define CWD		8 /* CHD dir/chdir */
#define FTPER	16 /* flag of error */
#define FTPOK	32 /* success */

struct requestHdr {
	int cmd; // request type
	int body_size; // size of raw data
};

int main(int argc, char *argv[])
{
    int sock, len;
    struct sockaddr_in addr;
    struct requestHdr rhdr;
	char szCmd[256], szdir[WRKDIR_LEN];
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        perror("socket");
        exit(1);
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        perror("connect");
        exit(2);
    }
	
	for(;;){
		memset(buf, 0, sizeof buf);
		printf("cmd: ");
		scanf("%s", szCmd);
		if(strcmp(szCmd, "ls")==0){
			rhdr.cmd = LIST;
			rhdr.body_size = 0;
			write(sock, &rhdr, sizeof(struct requestHdr));
			len = read(sock, &rhdr, sizeof(struct requestHdr));
			printf("readed = %d rhdr.body_size = %d\n", len, rhdr.body_size);
			
			if(rhdr.cmd & FTPOK){
				len = read(sock, buf, rhdr.body_size);
				printf("LEN:%d\n%s", len, buf);
			} else {
				printf("list fault..\n");
			}
		} else if (strcmp(szCmd, "cd")==0){
			rhdr.cmd = CWD;
			scanf("%s", szdir);
			rhdr.body_size = strlen(szdir)+1;
			memcpy(buf, &rhdr, sizeof(struct requestHdr));
			memcpy((char*)(buf+sizeof(struct requestHdr)), szdir, rhdr.body_size);
			write(sock, buf, sizeof(struct requestHdr)+rhdr.body_size);
		} else if(strcmp(szCmd, "close")==0){
			printf("\nclose conection..\n");
			break;
		}
		printf("\n----------------------\n");
	}
    
    getchar();
    close(sock);
    return 0;
}
