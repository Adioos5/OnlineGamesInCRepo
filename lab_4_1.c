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

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

volatile sig_atomic_t do_work = 1;

struct arguments {
	int serverSocket;
    int clientSocket;
	pthread_barrier_t *bar;
	sem_t *semaphore;
    int id;
	int sizeRead;
	int* winner;
	int* wholeGameWinner;
	int16_t* thisGameEnds;
	struct arguments* connections[3];
	char buff[MAXMSGSIZE];
};

struct arguments *connections[3];

int make_socket(int a, int b)
{
	int sock;
	sock = socket(a, b, 0);
	if (sock < 0)
		ERR("socket");
	return sock;
}

int bind_tcp_socket(uint16_t port)
{
	struct sockaddr_in addr;
	int socketfd, t = 1;
	socketfd = make_socket(PF_INET, SOCK_STREAM);
	memset(&addr, 0x00, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
		ERR("setsockopt");
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

void usage(char *name)
{
	fprintf(stderr, "USAGE: %s  port\n", name);
}

int who_wins(char** answers, int n)
{
	int result = -1, i, j, winsCounter;
	for(i = 0;i<n;i++)
	{
		winsCounter=0;
		for(j=0;j<n;j++)
		{
			if(j==i) continue;
            if(!strcmp(answers[i], "rock")){
                if(!strcmp(answers[j], "lizard") || !strcmp(answers[j], "scissors"))
						winsCounter++;
            } else if(!strcmp(answers[i], "paper")){
                if(!strcmp(answers[j], "rock") || !strcmp(answers[j], "Spock"))
						winsCounter++;
            } else if(!strcmp(answers[i], "scissors")){
                if(!strcmp(answers[j], "paper") || !strcmp(answers[j], "lizard"))
						winsCounter++;
            } else if(!strcmp(answers[i], "Spock")){
                if(!strcmp(answers[j], "scissors") || !strcmp(answers[j], "rock"))
						winsCounter++;
            } else if(!strcmp(answers[i], "lizard")){
                if(!strcmp(answers[j], "Spock") || !strcmp(answers[j], "paper"))
						winsCounter++;
            } 
		}
		if(winsCounter==n-1)
		{
			result = i;
			break;
		}
	}

	return result;
}

void* clientThreadWork(void* args)
{
    struct arguments* sessionArgs = (struct arguments*)args;

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
	
	send(sessionArgs->clientSocket, helloMsg, strlen(helloMsg),0);
	pthread_barrier_wait(sessionArgs->bar);
	send(sessionArgs->clientSocket, startMsg, strlen(startMsg),0);

	// GRAMY
	
	while(do_work && !(*sessionArgs->thisGameEnds)) {
		send(sessionArgs->clientSocket, playMsg, strlen(playMsg)+1, 0);
		int n = recv(sessionArgs->clientSocket, sessionArgs->buff, MAXMSGSIZE+1, 0);
		if(n==0) {
			*sessionArgs->thisGameEnds = 1;
			pthread_barrier_wait(sessionArgs->bar);
			break;
		}

		sessionArgs->sizeRead = n;
		sessionArgs->buff[strcspn(sessionArgs->buff, "\r\n")] = 0;

		send(sessionArgs->clientSocket, waitingMsg, strlen(waitingMsg)+1, 0);
		
		pthread_barrier_wait(sessionArgs->bar);
		
		send(sessionArgs->clientSocket, analyzeMsg, strlen(analyzeMsg)+1, 0);
		
		pthread_barrier_wait(sessionArgs->bar);
		
		if(*sessionArgs->winner==sessionArgs->id){
			send(sessionArgs->clientSocket, youWonThisRoundMsg, strlen(youWonThisRoundMsg)+1, 0);
		} else if(*sessionArgs->winner!=-1){
			send(sessionArgs->clientSocket, youLostThisRoundMsg, strlen(youLostThisRoundMsg)+1, 0);
		} else {
			send(sessionArgs->clientSocket, nobodyWonThisRoundMsg, strlen(nobodyWonThisRoundMsg)+1, 0);
		}

		if(*sessionArgs->wholeGameWinner>=0){
			if(*sessionArgs->wholeGameWinner==sessionArgs->id){
				send(sessionArgs->clientSocket, youWinMsg, strlen(youWinMsg)+1, 0);
			} else {
				send(sessionArgs->clientSocket, youLoseMsg, strlen(youLoseMsg)+1, 0);
			}
			pthread_barrier_wait(sessionArgs->bar);
			break;
		}

		pthread_barrier_wait(sessionArgs->bar);
	}

	//KONIEC
	
	sem_post(sessionArgs->semaphore);

	close(sessionArgs->clientSocket);
	free(sessionArgs);


	return NULL;
}

void* serverThreadWork(void* args)
{
    struct arguments* sessionArgs = (struct arguments*)args;
	int i, j, winner, roundsPlayed=0, wholeGameWinner=-1; 
	char buff[MAXMSGSIZE+1];
	char* answers[3];
	int points[3] = {0, 0, 0};
	for(i=0;i<3;i++)
		answers[i] = (char*)malloc((MAXMSGSIZE+1)*sizeof(char));

	pthread_barrier_wait(sessionArgs->bar);

	//GRAMY

	while(do_work && !(*sessionArgs->thisGameEnds)){
		pthread_barrier_wait(sessionArgs->bar);

		if(*sessionArgs->thisGameEnds==1) break;
		for(i = 0;i<3;i++){       
			printf("%d\n", sessionArgs->connections[i]->sizeRead);
			strncpy(answers[i], sessionArgs->connections[i]->buff, sessionArgs->connections[i]->sizeRead);
		}

		if((*sessionArgs->winner  = who_wins(answers, 3))!=-1)
		{
            points[*sessionArgs->winner]++;
		}

		for(i=0;i<3;i++)
		{
			if(points[i]==2)
			{
				*sessionArgs->wholeGameWinner=i;
				break;
			}
		}

		roundsPlayed++;

		pthread_barrier_wait(sessionArgs->bar);

		if(*sessionArgs->wholeGameWinner>=0 || roundsPlayed==5)
		{
			pthread_barrier_wait(sessionArgs->bar);
			*sessionArgs->thisGameEnds=1;
			break;
		}

		pthread_barrier_wait(sessionArgs->bar);

	}

	//KONIEC GRY

	free(sessionArgs);
	for(i=0;i<3;i++)
		free(answers[i]);

	return NULL;
}

void make_client_thread(int serverSocket, int clientSocket,
	pthread_barrier_t* bar, sem_t* semaphore, struct arguments* connections[3], int16_t* thisGameEnds, int i, int* winner, int* wholeGameWinner)
{
    struct arguments* args;
    if ((args = (struct arguments *)malloc(sizeof(struct arguments))) == NULL)
		ERR("malloc:");
	
    args->serverSocket = serverSocket;
    args->clientSocket = clientSocket;
	args->bar = bar;
	args->semaphore = semaphore;
	args->thisGameEnds = thisGameEnds;
	args->sizeRead=-1;
    args->id=i;
	args->winner = winner;
	args->wholeGameWinner = wholeGameWinner;

	connections[i] = args;

    pthread_t thread;
	if(pthread_create(&thread, NULL, clientThreadWork, (void*)args) != 0)
		ERR("pthread create");
	if(pthread_detach(thread) != 0)
		ERR("pthread detach");
}

void make_server_thread(int serverSocket, 
	pthread_barrier_t* bar, sem_t* semaphore, struct arguments* connections[3], int16_t* thisGameEnds, int* winner, int* wholeGameWinner)
{
    int i;
    struct arguments* args;
    if ((args = (struct arguments *)malloc(sizeof(struct arguments))) == NULL)
		ERR("malloc:");
	
    args->serverSocket = serverSocket;
    args->clientSocket = 0;
	args->bar = bar;
	args->semaphore = semaphore;
	args->thisGameEnds=thisGameEnds;
	args->winner = winner;
	args->wholeGameWinner = wholeGameWinner;
    for(i=0;i<3;i++){
		args->connections[i] = connections[i];
    }

    pthread_t thread;
	if(pthread_create(&thread, NULL, serverThreadWork, (void*)args) != 0)
		ERR("pthread create");
	if(pthread_detach(thread) != 0)
		ERR("pthread detach");
}

void doServer(int socket, int playersLimit)
{
    int clientSocket, i;
    sem_t semaphore;
    pthread_barrier_t *bar;

	if (sem_init(&semaphore, 0, playersLimit) != 0)
		ERR("sem_init");

    while (do_work) {
		struct arguments** connections = (struct arguments**)malloc(3*sizeof(struct arguments*));
		int16_t* thisGameEnds = (int16_t*)malloc(sizeof(int16_t));
		int* winner = (int*)malloc(sizeof(int));
		int* wholeGameWinner = (int*)malloc(sizeof(int));
		*winner=-1;
		*wholeGameWinner=-1;
		*thisGameEnds=0;
        bar = (pthread_barrier_t*)malloc(sizeof(pthread_barrier_t));
        pthread_barrier_init(bar, NULL, 4);

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
					make_client_thread(socket, clientSocket, bar, &semaphore, connections, thisGameEnds, i, winner, wholeGameWinner);

			}
		}

        make_server_thread(socket, bar, &semaphore, connections, thisGameEnds, winner, wholeGameWinner);
    }

}

int main(int argc, char** argv)
{
    int socket, playersLimit;
	if (argc != 3) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
    
	socket = bind_tcp_socket(atoi(argv[1]));
    playersLimit=atoi(argv[2]);

    doServer(socket, playersLimit);

    close(socket);

    return EXIT_SUCCESS;
}