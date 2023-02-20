#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <semaphore.h>
#include <sys/types.h>
#include <unistd.h>

#define BACKLOG 3
#define MAXMSGSIZE 100
#define THREAD_NUM 2

volatile sig_atomic_t do_work = 1;

void usage(char *name)
{
	fprintf(stderr, "USAGE: %s domain port time \n", name);
}

typedef struct {
	int id;
    int serverSocket;
	int clientSocket;
	int *condition;
    pthread_barrier_t* bar;
	pthread_cond_t *cond;
	pthread_mutex_t *mutex;
    char** answers;
} thread_arg;

struct sockaddr_in make_address(char *address, uint16_t port)
{
	struct sockaddr_in addr;
	struct hostent *hostinfo;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	hostinfo = gethostbyname(address);
	if (hostinfo == NULL)
		HERR("gethostbyname");
	addr.sin_addr = *(struct in_addr *)hostinfo->h_addr;
	return addr;
}


int make_socket(int domain, int type)
{
	int sock;
	sock = socket(domain, type, 0);
	if (sock < 0)
		ERR("socket");
	return sock;
}

int bind_inet_socket(char* address, uint16_t port, int type)
{
	struct sockaddr_in addr;
	int socketfd, t = 1;
	// stworzenie socketa z domeną SOCK_DGRAM
	socketfd = make_socket(PF_INET, type);
	// memsecik na adresie
	memset(&addr, 0, sizeof(struct sockaddr_in));
	// konfiguracja adresu
	addr = make_address(argv[2], argv[3]);
	// ustawienie opcji na deskryptor socketa
	if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t))) 
		ERR("setsockopt");
	
	// zbindowanie socketa z adresem
	if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		ERR("bind");
	
    if (listen(socketfd, BACKLOG) < 0)
		ERR("listen");
	
    return socketfd;
}

int add_new_client(int sfd)
{
	int nfd;
	if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, NULL, NULL))) < 0) {
		if (EAGAIN == errno || EWOULDBLOCK == errno)
			return -1;
		ERR("accept");
	}
	return nfd;
}

void* clientThreadWork(void* args)
{
    char* helloMsg = "Hello!\nWaiting for other players...\n";
	char* startMsg = "The game starts right now!\n";
    char* playMsg = "Type in your choice:\n";
	char* waitingMsg = "Waiting for other players to choose...\n";
	char* analyzeMsg = "Analyzing the results...\n";
	char* youWonThisRoundMsg = "You won this round!\n";
	char* youLostThisRoundMsg = "You lost this round!\n";
	char* nobodyWonThisRoundMsg = "Nobody won this round!\n";
	char* youWinMsg = "Congratulations! You win the game!\n";
	char* youLoseMsg = "You lose the game! :c Better luck next time!\n";
    char buff[MAXMSGSIZE];
    struct thread_arg* sessionArgs = (struct thread_arg*)args;
	
	pthread_barrier_wait(sessionArgs->bar);

	// GRAMY
	while(do_work) {
        if (pthread_mutex_lock(targ.mutex) != 0)
			ERR("pthread_mutex_lock");
        while (*sessionArgs->condition!=sessionArgs->id && work)
			if (pthread_cond_wait(targ.cond, targ.mutex) != 0)
				ERR("pthread_cond_wait");
        // Tura gracza

        send(sessionArgs->clientSocket, playMsg, strlen(playMsg)+1, 0);
		int n = recv(sessionArgs->clientSocket, buff, MAXMSGSIZE+1, 0);
        buff[strcspn(buff, "\r\n")] = 0;
        strncpy(sessionArgs->answers[sessionArgs->id], buff, n);

        // Koniec tury gracza
		*sessionArgs->condition = 1 - sessionArgs->id;
        if (pthread_mutex_unlock(targ.mutex) != 0)
			ERR("pthread_mutex_lock");
        if (pthread_cond_signal(sessionArgs->cond) != 0)
			ERR("pthread_cond_signal");

        pthread_barrier_wait(sessionArgs->bar);		
	}
	// KONIEC GRY
    close(sessionArgs->clientSocket);
	free(sessionArgs);

	return NULL;
}

void* serverThreadWork(void* args)
{
    struct thread_arg* sessionArgs = (struct thread_arg*)args;
    pthread_barrier_wait(sessionArgs->bar);
    while(do_work){
        //START GRY
        pthread_barrier_wait(sessionArgs->bar); // GRACZE JUŻ WPISALI ODPOWIEDZI

        if(strcmp(sessionArgs->answers[0], "stand") == 0 && strcmp(sessionArgs->answers[1], "stand") == 0) {

        }
    }
	free(sessionArgs);
    return NULL;
}

void make_client_thread(int clientSocket,
	pthread_barrier_t* bar, sem_t* semaphore, pthread_cond_t *cond, pthread_mutex_t *mutex, int *condition, int i, char** answers)
{
    struct thread_arg* args;
    if ((args = (struct thread_arg *)malloc(sizeof(struct thread_arg))) == NULL)
		ERR("malloc:");
	
    args->clientSocket = clientSocket;
	args->bar = bar;
	args->semaphore = semaphore;
    args->condition = condition;
    args->cond = cond;
    args->mutex = mutex;
    args->id=i;
    args->answers = answers;

    pthread_t thread;
	if(pthread_create(&thread, NULL, clientThreadWork, (void*)args) != 0)
		ERR("pthread create");
	if(pthread_detach(thread) != 0)
		ERR("pthread detach");
}

void make_server_thread(pthread_barrier_t* bar, sem_t* semaphore, 
    pthread_cond_t *cond, pthread_mutex_t *mutex, int *condition, char** answers)
{
    int i;
    struct thread_arg* args;
    if ((args = (struct thread_arg *)malloc(sizeof(struct thread_arg))) == NULL)
		ERR("malloc:");
	
	args->bar = bar;
	args->semaphore = semaphore;
    args->condition = condition;
    args->cond = cond;
    args->mutex = mutex;
    args->answers=answers;

    pthread_t thread;
	if(pthread_create(&thread, NULL, serverThreadWork, (void*)args) != 0)
		ERR("pthread create");
	if(pthread_detach(thread) != 0)
		ERR("pthread detach");
}

void doServer(int socket, int playersLimit, pthread_t *thread, thread_arg *targ, pthread_cond_t *cond, pthread_mutex_t *mutex, int *condition)
{
    int clientSocket, i;
    sem_t semaphore;
    pthread_barrier_t *bar;
	
    if (sem_init(&semaphore, 0, playersLimit) != 0)
		ERR("sem_init");

    while (do_work) {
        char** answers = (char**)malloc(2*sizeof(char*));
        answers[0] = (char*)malloc(MAXMSGSIZE*sizeof(char));
        answers[1] = (char*)malloc(MAXMSGSIZE*sizeof(char));

        bar = (pthread_barrier_t*)malloc(sizeof(pthread_barrier_t));
        pthread_barrier_init(bar, NULL, 3);

        // Dopuszczenie maksymalnej liczby graczy
		for(i = 0;i<3;i++){
			if (TEMP_FAILURE_RETRY(sem_trywait(&semaphore)) == -1) {
				switch (errno) {
					case EAGAIN:
						char* msg = "Server is full, try next time baby...\n";

						if((clientSocket=add_new_client(socket))!=-1) {
							send(clientSocket, msg, strlen(msg) + 1, 0);
							close(clientSocket);
						}
					case EINTR:
						continue;
				}
				ERR("sem_wait");
			} else {
				if((clientSocket=add_new_client(socket))!=-1)
					make_client_thread(clientSocket, bar, &semaphore, cond, mutex, condition, i, anwers);

			}
		}

        make_server_thread(socket, bar, &semaphore, cond, mutex, condition, answers);
    }
}

int main(int argc, char** argv)
{
    struct sockaddr_in addr;
    int socketfd, condition = 0;
    pthread_t thread[THREAD_NUM];
	thread_arg targ[THREAD_NUM];
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    if (argc != 4) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
    
    int playersLimit = atoi(argv[1]);
    socketfd = bind_inet_socket(argv[2], atoi(argv[3]), SOCK_STREAM);

    init(thread, targ, &cond, &mutex, &idlethreads, &cfd, &condition);
    doServer(socketfd, playersLimit, thread, targ, cond, mutex, &condition);

    return EXIT_SUCCESS;
}