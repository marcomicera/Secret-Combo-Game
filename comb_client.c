#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>	

#define COMBINATION_LENGTH 4
#define TIMEOUT_INTERVAL 60

// Utility
int i;
int timeoutNotExpired;

// Player info
int playing = 0;
int status = 0;	// STATUS DAL PUNTO DI VISTA DEL CLIENT
				// 0 : connecting
				// 1 : sending username size
				// 2 : sending username
				// 3 : sending UDP port number
				// 4 : commands
					// 41 : who
						// 4100 : receiving number of players			
						// 4101 : receiving username size
						// 4102 : receiving username
						// 4103 : receiving status
					// 42 : connect
						// 4200 : receiving opponent existence and availability
						// 4201 : receiving opponent response
						//
						// 4298 : receiving opponent username
						// 4299 : receiving opponent username size
				// 5 : game
					// 51 : game initialization
						// 5100 : receiving opponent IP size
						// 5101 : receiving opponent IP
						// 5102 : receiving opponent UDP port	
char* username;

// Game info
char secret_combination[COMBINATION_LENGTH + 1];
char my_guess[COMBINATION_LENGTH + 1];
char his_guess[COMBINATION_LENGTH + 1];
int my_turn = 0;
int udp_socket_created = 0;

// Server
int tcp_socket;
struct sockaddr_in server_address;

// UDP socket
uint32_t udp_port;
struct sockaddr_in address;
int udp_socket;

// Server communication
int bytes_received;
char* temp_buffer;
uint32_t server_response;	// 0 : no error
							// 1 : username already exists
							// 2 : opponent not found
uint32_t number_of_players;

// File descriptor table utility
int fdmax;
fd_set master;
fd_set read_fds;

// Terminal utility
#define BUFFER_LENGTH 22
char buffer[BUFFER_LENGTH];

// Avversario
struct player {
	int username_size;
	char* username;
	struct sockaddr_in address;
	int udp_port;
	int ip_size;
	char* ip;
} opponent;

//void printFDTstatus() { printf("\tfdmax: %d.\t", fdmax); fflush(stdout); for(i = 0; i <= fdmax; ++i) { if(FD_ISSET(i, &read_fds)) printf("1"); else printf("0"); fflush(stdout); } printf("\n"); fflush(stdout); }

void prompt() {
	if(!playing)
		printf("> ");
	else
		printf("# ");
	
	fflush(stdout);
}

void print_turn() {
	if(my_turn)
		printf("E' il tuo turno.\n");
	else
		printf("E' il turno di %s.\n", opponent.username);
		
	fflush(stdout);
}

void mdealloc(void** pointer) {
	if(*pointer != NULL) {
		free(*pointer);
		*pointer = NULL;
	}
}

void close_tcp_connection(int n) {
	if(!n) {
		printf("Connessione con il server chiusa.\n"); fflush(stdout);
	}
	else
		perror("recv() error");
	printf("Chiusura del programma.\n"); fflush(stdout);
	close(tcp_socket);
	exit(1);
}

void close_udp_connection(int n) {
	if(!n) {
		printf("Connessione con il server chiusa.\n"); fflush(stdout);
	}
	else
		perror("recv() error");
	printf("Chiusura del programma.\n"); fflush(stdout);
	close(udp_socket);
	exit(1);
}

void help_message() {
	printf(	"Sono disponibili i seguenti comandi:\n"
			"* !help  -->  mostra l'elenco dei comandi disponibili\n"
			"* !who  -->  mostra l'elenco dei client connessi al server\n"
			"* !connect nome_client  -->  avvia una partita con l'utente nome_client\n"
			"* !disconnect  -->  disconnette il client dall'attuale partita intrapresa con un altro peer\n"
			"* !combinazione comb  -->  permette al client di fare un tentativo con la combinazione comb\n"
			"* !quit  -->  disconnette il client dal server\n\n");
	fflush(stdout);
}

void game_over() {
	printf("Hai perso!\n\n"); fflush(stdout);

	//close(udp_socket);
	status = 4;
	udp_socket_created = 0;
	playing = 0;
	my_turn = 0;
	
	prompt();
}

void send_game_over() {
	int iWon = htonl(0);

	if(send(tcp_socket, (void*)&iWon, sizeof(iWon), 0) == -1) {
		perror("send() error");
	}
}

void win() {
	printf("Hai vinto!\n\n"); fflush(stdout);
	
	//close(udp_socket);
	status = 4;
	udp_socket_created = 0;
	playing = 0;
	my_turn = 0;
	
	prompt();
}

void send_win() {
	int iWon = htonl(1);

	if(send(tcp_socket, (void*)&iWon, sizeof(iWon), 0) == -1) {
		perror("send() error");
	}
}

void create_tcp_socket(const char* address, int port) {
	// Creazione del socket TCP
	if((tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket() error");
		printf("Chiusura del programma...\n"); fflush(stdout);
		exit(1);
	}
	//printf("TCP socket: %d.\n", tcp_socket); fflush(stdout);

	// Indirizzo del server socket
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;								// indirizzo di tipo IPv4
	server_address.sin_port = htons(port);								// porta
	if(inet_pton(AF_INET, address, &server_address.sin_addr) == 0) {	// IP
		perror("inet_pton() error");
		exit(1);
	}

	// Connessione al server
	if(connect(tcp_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
		perror("connect() error");
		exit(1);
	}
	status = 1; // sending username size
	printf("Connessione al server %s (porta %d) effettuata con successo\n\n", address, port); fflush(stdout);	
}

void create_udp_socket() {
	int yes = 1;
	
	// Creazione del socket UDP
	if((udp_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket() error");
		printf("Chiusura del programma...\n"); fflush(stdout);
		exit(1);
	}
	//printf("UDP socket: %d.\n", udp_socket); fflush(stdout);
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(udp_port);
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	if(setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		perror("setsockopt() error");
		exit(1);
	}
	if(bind(udp_socket, (struct sockaddr*)&address, sizeof(address)) == -1) {
		perror("bind() error");
		exit(1);
	}
	if(udp_socket > fdmax)
		fdmax = udp_socket;
	FD_SET(udp_socket, &master);

	// Inizializzazione dell'indirizzo dell'avversario
	memset(&opponent.address, 0, sizeof(opponent.address));
	opponent.address.sin_family = AF_INET;
	opponent.address.sin_port = htons(opponent.udp_port);
	if(inet_pton(AF_INET, opponent.ip, &opponent.address.sin_addr) == 0) {
		perror("inet_pton() error");
		exit(1);
	}
	
	/*if(connect(udp_socket, (struct sockaddr*)&opponent.address, sizeof(opponent.address)) == -1) {
		perror("connect() error");
		exit(1);
	}*/

	udp_socket_created = 1;
}

void set_username() {
	uint32_t username_size;
	
	printf("Inserisci il tuo nome (max 20 caratteri): "); fflush(stdout);

	while(status < 3) { // finnche' non ha ancora inviato la porta UDP
		fgets(buffer, BUFFER_LENGTH, stdin);
		strtok(buffer, "\n"); // elimina il \n dall'username
		if(strlen(buffer) > 20) {
			while(getchar() != '\n');
			printf("Inserire un nome non piu' lungo di 20 caratteri: "); fflush(stdout);
			continue;
		}
		username = (char*)malloc(strlen(buffer) + 1);
		strncpy(username, buffer, strlen(buffer));
		username[strlen(username)] = '\0';
		
		// Invio dell'username size
		username_size = htonl(strlen(buffer));
		/**/printf("Sto inviando la dimensione dell'username: %d.\n", ntohl(username_size)); fflush(stdout);
		if(send(tcp_socket, (void*)&username_size, sizeof(username_size), 0) == -1) {
			perror("send() error on username size");
		}
		status = 2; // sending username
		
		// Invio dell'username
		if(send(tcp_socket, (void*)username, strlen(username) + 1, 0) == -1) {
			perror("send() error on username");
		}

		// Username gia' presente
		if((bytes_received = recv(tcp_socket, &server_response, sizeof(server_response), 0)) <= 0)
			close_tcp_connection(bytes_received);
		else {
			if(server_response == 0)	// l'username e' ok
				status = 3; // sending UDP port number
			else {
				status = 1; // sending username size
				printf("Username non disponibile. Inserisci un nuovo nome: "); fflush(stdout);
			}
		}
	}
}

int all_letters() {
	for(i = 0; i < 4; ++i)
		if(buffer[i] < 97 || buffer[i] > 122)
			return 0;
	return 1;
}

void set_secret_combination(char* destination) {
	do { 
		fgets(buffer, BUFFER_LENGTH, stdin);
		strtok(buffer, "\n"); // elimina il \n
		if(strlen(buffer) != 4 || !all_letters()) {
			if(strlen(buffer) > 20)
				while(getchar() != '\n');
			printf("Inserire una combinazione di 4 caratteri: "); fflush(stdout);	
		}
	} while(strlen(buffer) != 4 || !all_letters());

	strncpy(destination, buffer, 4);
	destination[4] = '\0';
}

void set_udp_port() {
	printf("Inserisci la porta UDP di ascolto: "); fflush(stdout);
	do {
		scanf(" %d" "lu", &udp_port);
		//getchar(); // rimuove \n da stdin
		if(udp_port <= 1024 || udp_port > 65535) {
			while(getchar() != '\n');
			printf("Porta UDP non valida. Inserire una nuova porta UDP: "); fflush(stdout);
		}
	} while(udp_port <= 1024 || udp_port > 65535);
	udp_port = htonl(udp_port);
	
	// Invio della porta UDP
	printf("Sto inviando la porta UDP: %d.\n\n", ntohl(udp_port)); fflush(stdout);
	if(send(tcp_socket, (void*)&udp_port, sizeof(udp_port), 0) == -1) {
		perror("send() error on UDP port");
	}
	udp_port = ntohl(udp_port);
}

void fdt_init() {
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_SET(0, &master);
	FD_SET(tcp_socket, &master);
	fdmax = tcp_socket;
}

void send_who_command() {
	status = htonl(41); // (server) who
	if(send(tcp_socket, (void*)&status, sizeof(status), 0) == -1) {
		perror("send() error");
	}
	status = 4100; // who
}

void send_connect_command() {
	status = htonl(4200); // (server) receiving opponent username size
	if(send(tcp_socket, (void*)&status, sizeof(status), 0) == -1) {
		perror("send() error");
	}
	status = 4200; // receiving opponent existence and availability
	
	// Legge l'opponent username
	scanf("%s", buffer);
	getchar();
	opponent.username = (char*)malloc(strlen(buffer) + 1);
	strncpy(opponent.username, buffer, strlen(buffer));
	opponent.username[strlen(opponent.username)] = '\0';
	opponent.username_size = htonl(strlen(opponent.username));
	
	// Invia l'opponent username size
	if(send(tcp_socket, (void*)&opponent.username_size, sizeof(opponent.username_size), 0) == -1) {
		perror("send() error");
	}

	// Invia l'opponent username
	//printf("Vuoi giocare con: %s (lunghezza %d).\n", opponent.username, (int)strlen(opponent.username)); fflush(stdout);
	if(send(tcp_socket, (void*)opponent.username, strlen(opponent.username), 0) == -1) {
		perror("send() error");
	}
	
	my_turn = 1;
}

void send_combinazione_command() {
	set_secret_combination(my_guess);

	if(sendto(udp_socket, my_guess, strlen(my_guess) + 1, 0, (struct sockaddr*)&opponent.address, sizeof(opponent.address)) < 0) {
		perror("sendto() error");
		exit(1);
	}
	//printf("my_guess: %s, inviata all'IP %s, porta %d.\n", my_guess, inet_ntoa(opponent.address.sin_addr), opponent.address.sin_port);
}

void send_quit_command() {
	int quitting = htonl(9);
	
	if(udp_socket_created)
		send_game_over();
		
	if(send(tcp_socket, (void*)&quitting, sizeof(quitting), 0) == -1) {
		perror("send() error");
	}
	
	printf("Client disconnesso correttamente.\n"); fflush(stdout);
	
	// dealloca tutto
	mdealloc((void*)&username);
	mdealloc((void*)&opponent.username);
	mdealloc((void*)&opponent.ip);
	
	exit(1);
}

void send_disconnect_command() {
	status = 4;
	playing = 0;
	my_turn = 0;
	
	if(udp_socket_created) {
		send_game_over();
		printf("Disconnessione avvenuta con successo: TI SEI ARRESO\n\n"); fflush(stdout);
	}
	else {
		printf("Impossibile disconnettersi se non si e' in fase di gioco.\n\n"); fflush(stdout);
	}
	
	udp_socket_created = 0;
	prompt();
}

void read_command() {
	scanf(" %s", buffer);
	getchar(); // rimuove un eventuale spazio o \n da stdin

	if(strcmp(buffer, "!help") == 0) {
		help_message();
		prompt();
	}
	else if(strcmp(buffer, "!who") == 0 && !playing)
		send_who_command();
	else if(strcmp(buffer, "!connect") == 0)
		send_connect_command();
	else if(strcmp(buffer, "!combinazione") == 0 && playing && my_turn)
		send_combinazione_command();
	else if(strcmp(buffer, "!quit") == 0)
		send_quit_command();
	else if(strcmp(buffer, "!disconnect") == 0 && playing)
		send_disconnect_command();
	else {
		if(strcmp(buffer, "!connect") == 0 || strcmp(buffer, "!combinazione") == 0)
			while(getchar() != '\n');
		if(strcmp(buffer, "!combinazione") == 0 && playing && !my_turn)
			printf("Non e' il tuo turno.\n\n");
		else if(strcmp(buffer, "!disconnect") == 0 && !playing)
			printf("Non puoi disconnetterti se non stai giocando.\n\n");
		else
			printf("Comando non riconoscuto.\n\n");
		prompt();
	}
}

int guess_is_right() {
	char temp_secret_combination[COMBINATION_LENGTH + 1];
	uint32_t result[2];	// result[0]: numero di lettere giuste al posto giusto
						// result[1]: numero di lettere giuste al posto sbagliato
	int j;
	
	if(strcmp(secret_combination, his_guess) == 0) {
		game_over();
		send_game_over();
		return 1;
	}
	
	strncpy(temp_secret_combination, secret_combination, COMBINATION_LENGTH);
	temp_secret_combination[COMBINATION_LENGTH] = '\0';
	result[0] = 0;
	result[1] = 0;
	
	for(i = 0; i < COMBINATION_LENGTH; ++i)
		if(temp_secret_combination[i] == his_guess[i]) {
			++result[0];
			temp_secret_combination[i] = '+';
			his_guess[i] = '*';
		}
	
	for(i = 0; i < COMBINATION_LENGTH; ++i) {
		for(j = 0; j < COMBINATION_LENGTH; ++j) {
			if(his_guess[i] == temp_secret_combination[j]) {
				++result[1];
				temp_secret_combination[j] = '-';
				break;
			}
		}
	}

	result[0] = htonl(result[0]);
	result[1] = htonl(result[1]);

	if(sendto(udp_socket, result, sizeof(result), 0, (struct sockaddr*)&opponent.address, sizeof(opponent.address)) < 0) {
		perror("sendto() error");
		exit(1);
	}
	
	return 0;
}

int main(int argc, char** argv) {
	// Controllo dei parametri
	if(argc != 3) {
		fprintf(stderr, "Uso: comb_client <host remoto> <porta>\n"); fflush(stdout);
		exit(1);
	}

	// Socket per la comunicazione con il server
	create_tcp_socket(argv[1], atoi(argv[2]));

	// Messaggio iniziale
	help_message();

	// Lettura e invio dell'username
	set_username();

	// Lettura e invio della porta UDP
	set_udp_port();
	
	// Controllo della tastiera e del listening socket
	fdt_init();

	status = 4; // commands
	prompt();	
	
	while(1) {
		read_fds = master;
		if(udp_socket_created) {		
			// select() UDP
			struct timeval tv = { TIMEOUT_INTERVAL, 0 };
			if((timeoutNotExpired = select(fdmax + 1, &read_fds, NULL, NULL, &tv)) == -1) {
				perror("select() error");
				exit(1);
			}
			else if(timeoutNotExpired == 0) { // timer scaduto
				win();
				send_win();
			}

			// Controllo dei file descriptor relativi alla tastiera, all'avversario e al server
			if(FD_ISSET(0, &read_fds))						// 1) tastiera
				read_command();
			else if(FD_ISSET(udp_socket, &read_fds)) {		// 2) comunicazione dall'avversario
				if(my_turn) {	// 2.a) riceve la risposta dell'avversario
					int reply[2];
					
					socklen_t address_length = sizeof(opponent.address);
					if(recvfrom(udp_socket, reply, sizeof(reply), 0, (struct sockaddr*)&opponent.address, &address_length) < 0) {
						perror("recvfrom() error");
						exit(1);
					}
					else {
						char s1 = ((reply[0] = ntohl(reply[0])) == 1) ? 'a' : 'e', s2 = ((reply[1] = ntohl(reply[1])) == 1) ? 'a' : 'e';
						printf("%d letter%c giust%c al posto giusto, %d letter%c giust%c al posto sbagliato.\n", reply[0], s1, s1, reply[1], s2, s2); fflush(stdout);

						my_turn = 0;
					}
				}
				else {			// 2.b) riceve il tentativo dell'avversario
					socklen_t address_length = sizeof(opponent.address);
					if(recvfrom(udp_socket, his_guess, COMBINATION_LENGTH + 1, 0, (struct sockaddr*)&opponent.address, &address_length) < 0) {
						perror("recvfrom() error");
						exit(1);
					}
					else {
						printf("%s dice %s. ", opponent.username, his_guess); fflush(stdout);
						if(!guess_is_right()) {
							printf("Il suo tentativo e' sbagliato.\n"); fflush(stdout);
							prompt();
						
							my_turn = 1;
						}
					}
				}
			}
			else if(FD_ISSET(tcp_socket, &read_fds)) {		// 3) comunicazione dal server (fine partita)
				if((bytes_received = recv(tcp_socket, &server_response, sizeof(server_response), 0)) <= 0)
					close_tcp_connection(bytes_received);
				else {
					if((server_response = ntohl(server_response)) == 1) {
						win();
					}
					else {
						game_over();
					}
				}
			}
		}
		else {
			if(select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
				perror("select() error");
				exit(1);
			}

			for(i = 0; i <= fdmax; ++i) {
				if(FD_ISSET(i, &read_fds)) {
					if(i == 0)						// tastiera
						read_command();
					else if(i == tcp_socket) {		// comunicazione dal server
						switch(status) {
							case 4:		// commands
										if((bytes_received = recv(i, &status, sizeof(status), 0)) <= 0)
											close_tcp_connection(bytes_received);
										else
											status = ntohl(status);
										break;

							case 4100:	// who | receiving number of players
										if((bytes_received = recv(tcp_socket, &number_of_players, sizeof(number_of_players), 0)) <= 0)
											close_tcp_connection(bytes_received);
										else {
											if((number_of_players = ntohl(number_of_players)) == 0) {
												status = 4;
												printf("Non ci sono client connessi.\n\n"); fflush(stdout);
												prompt();
											}
											else {
												status = 4101;
												printf("%d client connessi al server: ", number_of_players); fflush(stdout);
											}
										}
										break;
										
							case 4101:	// who | receiving username size
										if((bytes_received = recv(tcp_socket, &server_response, sizeof(server_response), 0)) <= 0)
											close_tcp_connection(bytes_received);
										else {
											server_response = ntohl(server_response);
											temp_buffer = (char*)malloc(server_response + 1);
											status = 4102;
										}
										break;
										
							case 4102:	// who | receiving username
										if((bytes_received = recv(tcp_socket, temp_buffer, server_response + 1, 0)) <= 0)
											close_tcp_connection(bytes_received);
										else {
											printf("%s ", temp_buffer); fflush(stdout);
											mdealloc((void*)&temp_buffer);
											status = 4103;
										}
										break;
										
							case 4103:	// who | receiving status
										if((bytes_received = recv(tcp_socket, &server_response, sizeof(server_response), 0)) <= 0)
											close_tcp_connection(bytes_received);
										else {
											if((server_response = ntohl(server_response)) == 4)
												printf("(libero)");
											else
												printf("(occupato)"); 
											if(number_of_players > 1)
												printf(", ");
											fflush(stdout);
										}
										
										if(!--number_of_players) {
											status = 4;
											printf("\n\n");
											prompt();
										}
										else
											status = 4101;
										break;

							case 4200:	// connect | receiving opponent existence and availability
										if((bytes_received = recv(tcp_socket, &server_response, sizeof(server_response), 0)) <= 0)
											close_tcp_connection(bytes_received);
										else {
											if((server_response = ntohl(server_response)) == 0) {
												//printf("Richiesta di gioco inviata a %s. Attendi la sua risposta.\n", opponent.username); fflush(stdout);
												status = 4201;
											}
											else {
												printf("Impossibile connettersi a %s: utente inesistente o occupato.\n\n", opponent.username); fflush(stdout);
												status = 4;
												prompt();
											}
										}
										break;

							case 4201:	// connect | receiving opponent response
										if((bytes_received = recv(tcp_socket, &playing, sizeof(playing), 0)) <= 0)
											close_tcp_connection(bytes_received);
										else {
											if((playing = ntohl(playing)) == 1) {
												printf("L'avversario ha accettato.\n"); fflush(stdout);
												status = 5100;
											}
											else {
												printf("Impossibile connettersi a %s: l'utente ha rifiutato la partita.\n\n", opponent.username);
												prompt();
												status = 4;
											}
										}
										break;
										
							/* ........................................................ */
										
							case 4298:	// connect | receiving opponent username
										if((bytes_received = recv(tcp_socket, opponent.username, opponent.username_size + 1, 0)) <= 0)
												close_tcp_connection(bytes_received);

										printf("%s vuole giocare con te. Accetti? (y/n): ", opponent.username); fflush(stdout);
										do {
											scanf("%s", buffer);
											getchar(); // rimuove un eventuale spazio o \n da stdin
										
											if(strcmp(buffer, "y") == 0 || strcmp(buffer, "Y") == 0) {
												playing = htonl(1);
												status = 5100;					
											}
											else if(strcmp(buffer, "n") == 0 || strcmp(buffer, "N") == 0) {
												playing = htonl(0);
												status = 4;
												printf("\n");
												prompt();
											}
											else {
												printf("Risposta non valida. Accetti? (y/n): "); fflush(stdout);
											}
										} while(strcmp(buffer, "y") != 0 && strcmp(buffer, "Y") != 0 && strcmp(buffer, "n") != 0 && strcmp(buffer, "N") != 0);
										
										// Invia la risposta al server
										if(send(tcp_socket, (void*)&playing, sizeof(playing), 0) == -1) {
											perror("send() error");
										}
										playing = ntohl(playing);
										break;
										
							case 4299:	// connect | receiving opponent username size
										if((bytes_received = recv(tcp_socket, &opponent.username_size, sizeof(opponent.username_size), 0)) <= 0)
											close_tcp_connection(bytes_received);
										else {
											opponent.username_size = ntohl(opponent.username_size);
											opponent.username = (char*)malloc(opponent.username_size + 1);
											status = 4298;
										}
										break;
										
							case 5100:	// receiving opponent IP size
										if((bytes_received = recv(tcp_socket, &opponent.ip_size, sizeof(opponent.ip_size), 0)) <= 0)
											close_tcp_connection(bytes_received);
										else {
											opponent.ip_size = ntohl(opponent.ip_size);
											//printf("La lunghezza dell'IP dell'avversario e': %d\n", opponent.ip_size); fflush(stdout);
											opponent.ip = (char*)malloc(opponent.ip_size + 1);
											status = 5101;
										}
										break;
										
							case 5101:	// receiving opponent IP
										if((bytes_received = recv(tcp_socket, opponent.ip, opponent.ip_size + 1, 0)) <= 0)
											close_tcp_connection(bytes_received);
										else {
											//printf("L'IP dell'avversario e': %s\n", opponent.ip); fflush(stdout);
											status = 5102;
										}
										break;
										
							case 5102:	// receiving opponent UDP port
										if((bytes_received = recv(tcp_socket, &opponent.udp_port, sizeof(opponent.udp_port), 0)) <= 0)
											close_tcp_connection(bytes_received);
										else {
											opponent.udp_port = ntohl(opponent.udp_port);
											//printf("La porta udp dell'avversario e': %d.\n\n", opponent.udp_port); fflush(stdout);
											
											create_udp_socket();
											
											printf("Partita avviata con %s.\n", opponent.username); fflush(stdout);
											
											printf("Digita la tua combinazione segreta: "); fflush(stdout);
											set_secret_combination(secret_combination);
											print_turn();
											if(my_turn)
												prompt();
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
