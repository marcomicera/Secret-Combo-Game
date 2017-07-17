#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

// Utility
int bytes_received;

// Server address
struct sockaddr_in my_address;

// Gestione giocatori
#define BACKLOG 10
int listening_socket;
struct player* players = NULL;
int fdmax;
fd_set master;
fd_set read_fds;
int number_of_players = 0;

struct player {
	// Info
	uint32_t playing;
	int id;
	uint32_t username_size;
	char* username;
	int status;	// STATUS DAL PUNTO DI VISTA DEL SERVER
				// 0 : connecting
				// 1 : receiving username size
				// 2 : receiving username
				// 3 : receiving UDP port number
				// 4 : commands
					// 41 : who
					// 42 : connect
						// 4200 : receiving opponent username size
						// 4201 : receiving opponent username
						//
						//
						// 4299 : receiving opponent response
				// 5 : playing
				//
				// 9 : quitting

	// Connessione
	uint32_t ip_size;
	char* ip; // [7 : 15]
	int udp_port;
	struct sockaddr_in tcp_address;
	uint32_t error;	//-1 : game request
					// 0 : no error
					// 1 : username already exists
					// 2 : opponent not found or occupied

	// Avversario
	int opponent_username_size;	// di appoggio, prima di inizializzare
	char* opponent_username;	// il puntatore opponent
	struct player* opponent;
	
	// Partita
	int hasWon;
	
	// Lista
	struct player* next;
};

void mdealloc(void** pointer) {
	if(*pointer != NULL) {
		free(*pointer);
		*pointer = NULL;
	}
}

void create_listening_socket(const char* address, int port) {
	int yes = 1;
	
	// Creazione del listening socket
	if((listening_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket() error");
		exit(1);
	}
	
	// Indirizzo del listening socket
	memset(&my_address, 0, sizeof(my_address));
	my_address.sin_family = AF_INET;										// indirizzo di tipo IPv4
	my_address.sin_port = htons(port);										// porta
	if(inet_pton(AF_INET, address, &my_address.sin_addr.s_addr) == 0) {		// IP
		perror("inet_pton() error");
		exit(1);
	}
	if(setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		perror("setsockopt() error");
		exit(1);
	}

	// Collegamento di my_address a listening_socket
	if(bind(listening_socket, (struct sockaddr*)&my_address, sizeof(my_address)) == -1) {
		perror("bind() error");
		exit(1);
	}
	printf("Indirizzo: %s (Porta: %d)\n", address, port); fflush(stdout);
	
	// Il server inizia ad ascoltare sul listening socket
	if(listen(listening_socket, BACKLOG) == -1) {
		perror("listen() error");
		exit(1);
	}
}

void destroy_player(int);
void close_TCP_connection(int n, int socket) {
	if(!n) {
		printf("Connessione chiusa sul socket %d.\n", socket); fflush(stdout);
	}
	else
		perror("recv() error");
	close(socket);
	FD_CLR(socket, &master);
	destroy_player(socket);
}

void create_player() {
	socklen_t addrlen;
	int newfd;
	struct player* p = (struct player*)malloc(sizeof(struct player));
	
	//p->id = -1;
	addrlen = sizeof(p->tcp_address);
	if((newfd = accept(listening_socket, (struct sockaddr*)&p->tcp_address, &addrlen)) == -1) {
		perror("accept() error on player creation");
		mdealloc((void*)&p);
		printf("Player deleted.\n"); fflush(stdout);
	}
	else {
		p->ip_size = strlen(inet_ntoa(p->tcp_address.sin_addr));
		p->ip = (char*)malloc(p->ip_size + 1);
		strncpy(p->ip, inet_ntoa(p->tcp_address.sin_addr), strlen(inet_ntoa(p->tcp_address.sin_addr)));
		p->ip[p->ip_size] = '\0';
		p->next = players;
		players = p;

		FD_SET(newfd, &master);
		p->id = newfd;
		p->status = 1; // receiving username size
		p->playing = 0;
		if(newfd > fdmax)
			fdmax = newfd;
		//++number_of_players;
		printf("Connessione stabilita con il client\n"); fflush(stdout);
	}
}

// Cerca il giocatore di indice index
struct player* find_player(int index) { 	
	struct player* p = players;
	for(; p && p->id != index; p = p->next);
	return p;
}

// Cerca il giocatore di nome username con indice diverso da index
struct player* find_player_by_name(char* username, int index) {
	struct player* p = players;
	for(; p; p = p->next)
		if(p->id != index && p->status >= 4 && strcmp(p->username, username) == 0)
			break;

	return p;
}

void destroy_player(int index) {
	struct player *q = NULL, *p = players;
	
	for(; p && p->id != index; p = p->next) q = p;
	
	if(p) {
		mdealloc((void*)&p->username);
		mdealloc((void*)&p->opponent_username);
		mdealloc((void*)&p->opponent);
		
		if(!q)
			players = players->next;
		else
			q->next = p->next;

		if(p->status >= 4)
			--number_of_players;

		mdealloc((void*)&p);
	}
}

void send_who_response(int index) {
	struct player* p = players;
	uint32_t number_of_players_to_send = (number_of_players > 1) ? htonl(number_of_players - 1) : 0;
	
	/**/struct player *prova = find_player(index);
	/**/printf("Per %s ci sono %d giocatori online.\n", prova->username, ntohl(number_of_players_to_send)); fflush(stdout);
	
	if(send(index, (void*)&number_of_players_to_send, sizeof(number_of_players_to_send), 0) == -1) {
		perror("send() error on number_of_players_to_send in who response");
	}
	
	for(; p; p = p->next) {
		if(p->id != index && p->status >= 4) {
			// username size
			p->username_size = htonl(p->username_size);
			if(send(index, (void*)&p->username_size, sizeof(p->username_size), 0) == -1) {
				perror("send() error on username size in who response");
			}
			p->username_size = ntohl(p->username_size);
			
			// username
			/**/printf("Sto inviando l'username del player di id %d: %s (lunghezza %d).\n", p->id, p->username, (int)strlen(p->username)); fflush(stdout);
			if(send(index, (void*)p->username, strlen(p->username) + 1, 0) == -1) {
				perror("send() error on username in who response");
			}
			
			// status
			p->status = htonl(p->status);
			if(send(index, (void*)&p->status, sizeof(p->status), 0) == -1) {
				perror("send() error on status in who response");
			}
			p->status = ntohl(p->status);
		}
	}
}

int main(int argc, char** argv) {
	int i;
	
	// Controllo dei parametri
	if(argc != 3) {
		fprintf(stderr, "Uso: comb_server <host> <porta>\n"); fflush(stdout);
		exit(1);
	}
	
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	
	create_listening_socket(argv[1], atoi(argv[2]));
	
	// Controllo del listening socket
	FD_SET(listening_socket, &master);
	fdmax = listening_socket;

	while(1) {
		read_fds = master;
		if(select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
			perror("select() error");
			exit(1);
		}
		
		for(i = 0; i <= fdmax; ++i) {
			if(FD_ISSET(i, &read_fds)) {
				if(i == listening_socket) {	// nuova connessione
					create_player();
				}
				else {						// dati
					struct player* client = find_player(i);
					
					if(client != NULL) {
						switch(client->status) {
							case 1: // receiving username size
									if((bytes_received = recv(i, &client->username_size, sizeof(client->username_size), 0)) <= 0)
										close_TCP_connection(bytes_received, i);
									else {
										client->username_size = ntohl(client->username_size);
										if(client->username_size > 0) {
											client->username = (char*)malloc(client->username_size + 1);
											client->status = 2;
											/**/printf("L'username e' lungo %d Bytes.\n", client->username_size); fflush(stdout);
										}
										else {	// lunghezza <= 0
											
										}
									}
									break;
									
							case 2:	// receiving username
									if((bytes_received = recv(i, client->username, client->username_size + 1, 0)) <= 0)
										close_TCP_connection(bytes_received, i);
									else {
										if(find_player_by_name(client->username, i) != NULL)
											client->error = 1;
										else
											client->error = 0;
										
										// Dice al client se l'username gia' esiste
										if(send(i, (void*)&client->error, sizeof(client->error), 0) == -1) {
											perror("send() error about duplicate username");
										}

										if(client->error == 0) {	// l'username e' ok
											client->status = 3;
											/**/printf("L'username e' %s.\n", client->username); fflush(stdout);
										}
										else {				// l'username gia' esisteva
											mdealloc((void*)&client->username);
											client->status = 1;
											/**/printf("L'username e' gia' esistente.\n"); fflush(stdout);
										}
									}
									//printf("\nHo ricevuto %d Bytes. ", bytes_received); fflush(stdout);
									break;
									
							case 3:	// receiving UDP port number
									if((bytes_received = recv(i, &client->udp_port, sizeof(client->udp_port), 0)) <= 0)
										close_TCP_connection(bytes_received, i);
									else {
										client->udp_port = ntohl(client->udp_port);
										client->status = 4;
										/**/printf("La porta UDP di %s e' %d.\n", client->username, client->udp_port); fflush(stdout);
										++number_of_players;
										printf("%s si e' connesso\n", client->username);
									}
									break;
									
							case 4: // commands
									if((bytes_received = recv(i, &client->status, sizeof(client->status), 0)) <= 0)
										close_TCP_connection(bytes_received, i);
									else {
										client->status = ntohl(client->status);

										// gestione di comandi che non prevedono recv()
										switch(client->status) {
											case 41:	// who
														/**/printf("Ci sono in totale %d giocatori online.\n", number_of_players); fflush(stdout);
														send_who_response(i);
														client->status = 4;
														break;
														
											case 9:		// quitting
														/**/printf("Distruggo %s.\n", client->username); fflush(stdout);
														destroy_player(i);
														break;
										}
									}
									break;

							case 4200:	// connect | receiving opponent username size
										if((bytes_received = recv(i, &client->opponent_username_size, sizeof(client->opponent_username_size), 0)) <= 0)
											close_TCP_connection(bytes_received, i);
										else {
											client->opponent_username_size = ntohl(client->opponent_username_size);
											mdealloc((void*)&client->opponent_username);
											client->opponent_username = (char*)malloc(client->opponent_username_size + 1);
											/**/printf("La lunghezza dell'username dell'avversario è: %d.\n", client->opponent_username_size); fflush(stdout);
											client->status = 4201;
										}
										break;

							case 4201:	// connect | receiving opponent username
										if((bytes_received = recv(i, client->opponent_username, client->opponent_username_size + 1, 0)) <= 0)
											close_TCP_connection(bytes_received, i);
										else {
											client->opponent_username[client->opponent_username_size] = '\0';
											if((client->opponent = find_player_by_name(client->opponent_username, i)) != NULL && !client->opponent->playing && client->opponent->status == 4) {
												client->opponent->opponent = client;
												/**/printf("%s vuole giocare con %s, che esiste e non e' occupato.\n", client->username, client->opponent_username); fflush(stdout);

												client->opponent->status = htonl(4299);
												if(send(client->opponent->id, (void*)&client->opponent->status, sizeof(client->opponent->status), 0) == -1)
													perror("send() error");
												client->opponent->status = 4299;
											
												// Invia l'username size del proponente all'avversario
												if(send(client->opponent->id, (void*)&client->username_size, sizeof(client->username_size), 0) == -1)
													perror("send() error");
												
												// Invia l'username del proponente all'avversario
												if(send(client->opponent->id, (void*)client->username, strlen(client->username), 0) == -1)
													perror("send() error");
												
												client->error = htonl(0);
												//client->status = 5;
											}
											else {
												/**/printf("%s ha provato a giocare con %s, che non esiste o e' occupato.\n", client->username, client->opponent_username); fflush(stdout);
												client->error = htonl(2);
												client->status = 4;
											}
											
											mdealloc((void*)&client->opponent_username);

											// Avverte il client proponente riguardo l'esistenza e la disponibilita' dell'avversario
											if(send(i, (void*)&client->error, sizeof(client->error), 0) == -1)
												perror("send() error");
										}
										break;

							/* ........................................................ */

							case 4299:	// connect | receiving opponent response
										if((bytes_received = recv(i, &client->playing, sizeof(client->playing), 0)) <= 0)
											close_TCP_connection(bytes_received, i);
										else {
											client->playing = ntohl(client->playing);
											if(client->playing == 1) {	// richiesta accettata
												client->opponent->playing = htonl(1); // proponente
												printf("%s ha accettato di giocare con %s.\n", client->username, client->opponent->username); fflush(stdout);
												client->status = 5;
												client->opponent->status = 5;
											}
											else {						// richiesta rifiutata	
												printf("%s ha rifiutato di giocare con %s.\n", client->username, client->opponent->username); fflush(stdout);
												client->opponent->playing = htonl(0); // proponente
												client->status = 4;
												client->opponent->status = 4;
											}
										}

										if(send(client->opponent->id, (void*)&client->opponent->playing, sizeof(client->opponent->playing), 0) == -1)
												perror("send() error");
										client->opponent->playing = ntohl(client->opponent->playing);
										
										if(client->playing == 1) {
											// a se' stesso (opponent)
											client->opponent->ip_size = htonl(client->opponent->ip_size);
											if(send(client->id, (void*)&client->opponent->ip_size, sizeof(client->opponent->ip_size), 0) == -1)
													perror("send() error");
											client->opponent->ip_size = ntohl(client->opponent->ip_size);
											if(send(client->id, (void*)client->opponent->ip, strlen(client->opponent->ip) + 1, 0) == -1)
													perror("send() error");	
											/**/printf("Sto inviando la porta %d di %s a %s.\n", client->opponent->udp_port, client->opponent->username, client->username); fflush(stdout);
											client->opponent->udp_port = htonl(client->opponent->udp_port);
											if(send(client->id, (void*)&client->opponent->udp_port, sizeof(client->opponent->udp_port), 0) == -1)
													perror("send() error");
											client->opponent->udp_port = ntohl(client->opponent->udp_port);

											// al proponente
											client->ip_size = htonl(client->ip_size);
											if(send(client->opponent->id, (void*)&client->ip_size, sizeof(client->ip_size), 0) == -1)
													perror("send() error");
											client->ip_size = ntohl(client->ip_size);
											if(send(client->opponent->id, (void*)client->ip, strlen(client->ip) + 1, 0) == -1)
													perror("send() error");
											/**/printf("Sto inviando la porta %d di %s a %s.\n", client->udp_port, client->username, client->opponent->username); fflush(stdout);
											client->udp_port = htonl(client->udp_port);
											if(send(client->opponent->id, (void*)&client->udp_port, sizeof(client->udp_port), 0) == -1)
													perror("send() error");
											client->udp_port = ntohl(client->udp_port);
										}
										break;

							case 5:		// playing (fine partita)
										if((bytes_received = recv(i, &client->hasWon, sizeof(client->hasWon), 0)) <= 0)
											close_TCP_connection(bytes_received, i);
										else {
											struct player *winner, *loser;

											if((client->hasWon = ntohl(client->hasWon))) {
												winner = client;
												loser = client->opponent;
												loser->hasWon = htonl(0);
												if(send(loser->id, (void*)&loser->hasWon, sizeof(loser->hasWon), 0) == -1)
													perror("send() error");
											}
											else {
												winner = client->opponent;
												loser = client;
												winner->hasWon = htonl(1);
												if(send(winner->id, (void*)&winner->hasWon, sizeof(winner->hasWon), 0) == -1)
													perror("send() error");
											}

											/**/printf("%s ha vinto e %s ha perso.\n", winner->username, loser->username); fflush(stdout);

											winner->status = 4;
											winner->playing = 0;
											loser->status = 4;
											loser->playing = 0;
										}
										break;

							default:	
										break;
						}
					}
				}
			}
		}
	}

	return 0;
}
