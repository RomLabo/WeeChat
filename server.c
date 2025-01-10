/*
0000000001 Author RomLabo 111111111
1000111000 server.c 111111111111111
1000000001 Created on 08/11/2024 11
10001000111110000000011000011100001
10001100011110001100011000101010001
00000110000110000000011000110110001
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <time.h>

/*
    Constantes
*/

// Paramètres socket
const int sock_domain = AF_INET;
const int sock_type = SOCK_STREAM;
const int sock_protocol = 0;
const int port = 8080;
const char addr_inet[10] = "127.0.0.1";

// Paramètyres généraux
const int buffer_size = 512;
const int max_channels = 10;
const char acquit_msg[8] = "SYN-ACK";
const char exit_msg[4] = "EXT";
const int path_history_size = 140;
const int line_history_size = 768;

// Paramètres stockage clients
const int clients_min_size = 6;
const int clients_gap_size = 4;

// Code erreur de l'application
enum error_type {
    ERR_TOO_MUCH_ARG = -20, 
    ERR_CREATE_SOCKET,
    ERR_BIND_SOCKET, 
    ERR_LISTEN_SOCKET,
    ERR_CONNECT_SERVER, 
    ERR_READ_ACK, 
    ERR_HANDLE_SIG, 
    ERR_READ_CHANNELS,
    ERR_CLIENTS_STORAGE,
    ERR_ADD_CLIENT, 
    ERR_MAX_CHANNELS,
    ERR_CREATE_CHANNEL_SOCKET, 
    ERR_BIND_CHANNEL_SOCKET,
    ERR_LISTEN_CHANNEL_SOCKET, 
    ERR_SEND_HISTORY,
    ERR_READ_HISTORY,
    ERR_WRITE_HISTORY
};

/*
    Variables
*/

int main_socket;
int off_server = 0;
time_t current_time;
struct tm * m_time; 
int id_clients = 0;

int clients_curr_size = 6;
int* clients = NULL;
int clients_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER; 
pid_t child_pids[10];
int child_count = 0;

typedef struct {
    char name[50];
    char ip[18];
    int port;
} Channel;

typedef struct {
    int socket;
    const char* channel_name;
} Client;

/*
    Déclaration des fonctions
*/

void setup_addr(int argc, char *argv[], struct sockaddr_in *addr);
void handle_signal(int sig);

int send_history(Client* client);
void add_date_msg(char* buffer_msg_date, char* buffer_msg, size_t size_msg_date);
int save_msg(const char* channel_name, const char* msg);
void broadcast_msg(const char* msg);

int remove_client(int** all_clients, Client* client);
int add_client(int** all_clients, int socket);

int add_channel(Channel channels[], int* count, const char* ip, const char* name, int port);

void handle_client(void* client_socket);
void start_channel(const char* name, const char* ip, int port);

void send_acquit(int* socket);
void wait_acquit(int* socket);

/*
    Main
*/

int main(int argc, char *argv[]) {
    struct sigaction action;
    struct sockaddr_in addr_client, main_addr_server;
    socklen_t addr_len = sizeof(addr_client);
    Channel channels[max_channels];
    int channels_count = 0;
    char ip_server[18];
    int last_port = 0;
    
    if (argc > 1) { strncpy(ip_server, argv[1], sizeof(ip_server)); }
    else { strncpy(ip_server, addr_inet, sizeof(ip_server)); }
    
    printf("[INFO] Démarrage du serveur\n");

    if (argc > 3) {
        printf("[ERROR] Trop d arguments renseignés \n");
        printf("[INFO] Arrêt du serveur\n");
        return ERR_TOO_MUCH_ARG;
    }

    /* Configuration de l'adresse du serveur */
    setup_addr(argc, argv, &main_addr_server);

    /* Gestion des signaux */
    action.sa_handler = &handle_signal;
    if (sigaction(SIGINT, &action, NULL) < 0) {
        perror("[ERROR] Gestion signaux impossible\n");
        return ERR_HANDLE_SIG;
    }

    main_socket = socket(sock_domain, sock_type, sock_protocol);
    if (main_socket < 0) {
        perror("[ERROR] Création socket impossible\n");
        return ERR_CREATE_SOCKET;
    }

    if (bind(main_socket, (struct sockaddr *)&main_addr_server, sizeof(main_addr_server)) < 0) {
        close(main_socket);
        perror("[ERROR] Liaison socket impossible\n");
        return ERR_BIND_SOCKET;
    }

    if (listen(main_socket, 5) < 0) {
        close(main_socket);
        perror("[ERROR] Ecoute impossible\n");
        return ERR_LISTEN_SOCKET;
    }

    FILE* file = fopen("data/channels.txt", "r");
    if (file == NULL) {
        close(main_socket);
        perror("[ERROR] Lecture channels impossible\n");
        return ERR_READ_CHANNELS;
    }

    printf("[INFO] Démarrage ecoute connexion\n");
    printf("[CONFIG] %s:%d\n", addr_inet, port);

    char line[100];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;
        char* token = strtok(line, ",");
        if (token == NULL) { continue; }

        char channel_name[100];
        strncpy(channel_name, token, sizeof(channel_name)); 
        token = strtok(NULL, ",");
        if (token == NULL) { continue; }

        int port = atoi(token);
        last_port = port + 1;

        if (add_channel(channels, &channels_count, ip_server, channel_name, port) < 0) {
            printf("[ERROR] Nombre maximal de channels atteint\n");
            continue; 
        }

        pid_t pid = fork();
        if (pid == 0) {
            start_channel(channel_name, ip_server, port);
            exit(0);
        } else if (pid > 0) {
            child_pids[child_count] = pid;
            child_count ++;
        } else { perror("[ERROR] Démarrage des channels impossible\n"); }
    }

    fclose(file);

    while (off_server == 0) {
        int* new_client_socket = (int*)malloc(sizeof(int));
        if (new_client_socket == NULL) {
            perror("[ERROR] Allocation socket client impossible\n");
            continue;
        }

        (*new_client_socket) = accept(main_socket, (struct sockaddr*)&addr_client, &addr_len);
        if ((*new_client_socket) < 0 && off_server == 0) {
            perror("[ERROR] Connexion client impossible\n");
            free(new_client_socket);
            continue;
        }

        if (off_server == 0) {
            /* Envoie acquittement connexion au server principal */
            send_acquit(new_client_socket);

            /* Attente acquittement réception du client */
            wait_acquit(new_client_socket);

            char buffer[buffer_size];
            snprintf(buffer, sizeof(buffer), "Bienvenue sur ServerClientChat !\n Choisissez un channel :\n");

            for (int i = 0; i < channels_count; i++) {
                char number[20];
                int choiceNum = i + 1;
                snprintf(number, sizeof(number), "%d) ", choiceNum);
                strcat(buffer, number);
                strcat(buffer, channels[i].name);
                strcat(buffer, "\n");
            }
            char lastNumber[40];
            int lastChoiceNum = channels_count + 1;
            snprintf(lastNumber, sizeof(lastNumber), "%d) Créer un channel\n", lastChoiceNum);
            strcat(buffer, lastNumber);

            /* Envoi du menu principal */
            if (write((*new_client_socket), buffer, strlen(buffer) + 1) < 0) {
                perror("[ERROR] Envoi menu impossible\n");
                close((*new_client_socket));
                free(new_client_socket);
                continue;
            }

            /* Réception du choix de channel */
            int nb_bytes = read((*new_client_socket), buffer, sizeof(buffer));
            if (nb_bytes <= 0) {
                perror("[ERROR] Réception choix channel impossible\n");
                close((*new_client_socket));
                free(new_client_socket);
                continue;
            } 

            buffer[nb_bytes] = '\0';
            int choice = atoi(buffer);

            /* Envoie au client ip et port du channel */
            if (choice > 0 && choice <= channels_count) {
                char port_str[6];
                char temp_addr[26];
                snprintf(port_str, sizeof(port_str), "%d", channels[(choice - 1)].port);
                strncpy(temp_addr, channels[(choice - 1)].ip, sizeof(temp_addr));
                strcat(temp_addr, ":");
                strcat(temp_addr, port_str);
                snprintf(buffer, sizeof(buffer), "%s\n", temp_addr);
            } else if (choice == (channels_count + 1)) {
                snprintf(buffer, sizeof(buffer), "Saisir le nom du channel :");
            } else { 
                snprintf(buffer, sizeof(buffer), "Choix invalide\n");
                choice = -1; 
            }

            if (write((*new_client_socket), buffer, sizeof(buffer)) < 0) {
                perror("[ERROR] Envoi info channel impossible\n");
                close((*new_client_socket));
                free(new_client_socket);
                continue;
            }

            if (choice <= channels_count) {
                close((*new_client_socket));
                free(new_client_socket);
                continue;
            }

            char buffer_create[buffer_size];
            nb_bytes = read((*new_client_socket), buffer_create, sizeof(buffer_create));
            if (nb_bytes <= 0) { 
                perror("[ERROR] Réception nom du channel impossible\n");
                continue;
            }

            FILE* file = fopen("data/channels.txt", "a");
            if (file == NULL) {
                perror("[ERROR] Sauvegarde message impossible\n");
                continue;
            } 

            char new_channel[buffer_size];
            char new_port[8];
            buffer_create[nb_bytes] = '\0';
            strncpy(new_channel, buffer_create, sizeof(new_channel));
            snprintf(new_port, sizeof(new_port), ",%d\n", last_port);
            strcat(new_channel, new_port);
            printf("%s\n", new_channel);
            fprintf(file, "%s", new_channel);
            fclose(file);

            if (add_channel(channels, &channels_count, ip_server, buffer_create, last_port) < 0) {
                printf("[ERROR] Nombre maximal de channels atteint\n");
                continue; 
            }

            char port_str[6];
            char temp_addr[26];
            snprintf(port_str, sizeof(port_str), "%d", last_port);
            strncpy(temp_addr, ip_server, sizeof(temp_addr));
            strcat(temp_addr, ":");
            strcat(temp_addr, port_str);
            snprintf(buffer_create, sizeof(buffer_create), "%s", temp_addr);
            if (write((*new_client_socket), buffer_create, strlen(buffer_create) +1) < 0) {
                perror("[ERROR] Envoi info nouveau channel impossible\n"); 
            }

            pid_t pid = fork();
            if (pid == 0) {
                start_channel(buffer_create, ip_server, last_port);
                exit(0);
            } else if (pid > 0) {
                child_pids[child_count] = pid;
                child_count ++;              
            } else { perror("[ERROR] Démarrage des channels impossible\n"); }
            
            close((*new_client_socket));
            free(new_client_socket);
        }
    }

    close(main_socket);
    printf("[INFO] Arrêt du serveur\n");
    return EXIT_SUCCESS;
}

/*
    Définition des fonctions
*/

void setup_addr(int argc, char *argv[], struct sockaddr_in *addr) {
    (*addr).sin_family = sock_domain;
    (*addr).sin_addr.s_addr = inet_addr(addr_inet);
    (*addr).sin_port = htons(port);

    if (argc == 3) {
        (*addr).sin_addr.s_addr = inet_addr(argv[1]);
        (*addr).sin_port = htons(atoi(argv[2]));
    } else if (argc == 2) {
        (*addr).sin_addr.s_addr = inet_addr(argv[1]);
        (*addr).sin_port = htons(port);
    }
}

void handle_signal(int signal) {
    if (signal == SIGCHLD) {
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            // gérer les processus zombie
        }
    } else if (signal == SIGINT) {
        printf("[INFO] Arrêt serveur principal et channels en cours...\n");
        for (int i = 0; i < child_count; i++) {
            kill(child_pids[i], SIGTERM);
        }

        off_server = 1;
        printf("\n");
        close(main_socket);
    }
}

int send_history(Client* client) {
    int error = 0;

    char* path = (char*)malloc(sizeof(char) * path_history_size);
    if (path == NULL) { return ERR_SEND_HISTORY; }

    char* line = (char*)malloc(sizeof(char) * line_history_size);
    if (line == NULL) {
        free(path); 
        return ERR_SEND_HISTORY; 
    }

    pthread_mutex_lock(&clients_mutex);
    snprintf(path, sizeof(char) * path_history_size, "data/%s", (*client).channel_name);
    strcat(path, "_h.txt");
    FILE* file = fopen(path, "r");

    if (file != NULL) {
        while (fgets(line, sizeof(line), file) != NULL) {
            if (write((*client).socket, line, strlen(line) + 1) < 0) {
                error = ERR_SEND_HISTORY;
                break;
            }
        }
    } else { error = ERR_READ_HISTORY; }

    fclose(file);
    free(path);
    free(line);
    pthread_mutex_unlock(&clients_mutex);
    return error; 
}

void add_date_msg(char* buffer_msg_date, char* buffer_msg, size_t size_msg_date) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    snprintf(buffer_msg_date, size_msg_date, "%d-%02d-%02d %02d:%02d:%02d : ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    strcat(buffer_msg_date, buffer_msg);
    buffer_msg_date[size_msg_date] = '\0';
}

int save_msg(const char* channel_name, const char* msg) {
    int error = 0;

    char* path = (char*)malloc(sizeof(char) * path_history_size);
    if (path == NULL) { return ERR_WRITE_HISTORY; }

    pthread_mutex_lock(&clients_mutex);
    snprintf(path, sizeof(char) * path_history_size, "data/%s", channel_name);
    strcat(path, "_h.txt");
    FILE* file = fopen(path, "a+");

    if (file != NULL) {
        fprintf(file, "%s", msg);
    } else { error = ERR_WRITE_HISTORY; } 
         
    fclose(file);
    free(path);
    pthread_mutex_unlock(&clients_mutex);
    return error;
}

void broadcast_msg(const char* msg) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < clients_count; i++) {
        if (write(clients[i], msg, strlen(msg) + 1) < 0) {
            perror("[ERROR] Diffusion du message impossible\n");
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

int add_client(int** all_clients, int socket) {
    /* Augmentation en mémoire stockage des socket clients */
    if (clients_count >= (clients_curr_size - clients_gap_size)) {
        int new_size = clients_curr_size + clients_min_size;
        (*all_clients) = realloc((*all_clients), new_size * sizeof(int));

        if ((*all_clients) == NULL) { return ERR_ADD_CLIENT; } 
        else { clients_curr_size = new_size; }
    }

    (*all_clients)[clients_count] = socket;
    clients_count ++;
    return 0; 
}

int remove_client(int** all_clients, Client* client) {
    int i = 0;
    while (i < clients_count) {
        if ((*all_clients)[i] == (*client).socket) {
            clients_count --;
            (*all_clients)[i] = (*all_clients)[clients_count];
            break;
        }
        i ++;
    }

    /* Réduction en mémoire stockage des sockets clilents */
    if (clients_count <= (clients_curr_size - clients_gap_size - clients_min_size)) {
        int new_size = clients_curr_size - clients_min_size;
        if (new_size > 0) {
            (*all_clients) = realloc((*all_clients), new_size * sizeof(int));
            if ((*all_clients) == NULL) { return ERR_CLIENTS_STORAGE; } 
            else { clients_curr_size = new_size; }
        }
    }

    return 0;
}

int add_channel(Channel channels[], int* count, const char* ip, const char* name, int port) {
    if ((*count) >= max_channels) { return ERR_MAX_CHANNELS; }
    snprintf(channels[(*count)].ip, sizeof(channels[(*count)].ip), "%s", ip);
    snprintf(channels[(*count)].name, sizeof(channels[(*count)].name), "%s", name);
    channels[(*count)].port = port;
    (*count) ++;
    return 0;
}

void handle_client(void* client) {
    Client* client_ptr = (Client*)client;
    int nb_bytes = 1;
    size_t msg_size = 0; 
    size_t msg_date_size = 0; 
    int date_size = 75;

    if (send_history(client_ptr) < 0) {
        perror("[ERROR] Envoi historique impossible\n");
    }

    while (nb_bytes > 0 && off_server == 0) {
        if (read((*client_ptr).socket, &msg_size, sizeof(size_t)) <= 0) {
            continue;
        } 

        msg_date_size = msg_size + date_size;
        
        char* msg = (char*)malloc(sizeof(char) * msg_size);
        char* msg_with_date = (char*)malloc(sizeof(char) * msg_date_size);
        if (msg == NULL || msg_with_date == NULL) {
            perror("[ERROR] Allocation message client impossible\n");
            continue;
        }

        nb_bytes = read((*client_ptr).socket, msg, sizeof(char) * msg_size);
        msg[nb_bytes] = '\0';

        add_date_msg(msg_with_date, msg, msg_date_size);
        if (save_msg((*client_ptr).channel_name, msg_with_date) < 0) {
            perror("[ERROR] Sauvegarde message impossible\n");
        }

        broadcast_msg(msg_with_date);
        free(msg);
        free(msg_with_date);
    }

    pthread_mutex_lock(&clients_mutex);

    if (remove_client(&clients, client_ptr) < 0) {
        perror("[ERROR] Réduction stockage clients impossible\n");
    }

    close((*client_ptr).socket);
    free(client_ptr);
    pthread_mutex_unlock(&clients_mutex);

    printf("[INFO] Client déconnecté\n");
    pthread_exit(NULL);
}

void start_channel(const char* name, const char* ip, int port) {
    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    clients = (int*)malloc(clients_min_size * sizeof(int));
    if (clients == NULL) {
        perror("[ERROR] Allocation stockage clients impossible\n");
        exit(ERR_CLIENTS_STORAGE);
    }

    server_socket = socket(sock_domain, sock_type, 0);
    if (server_socket < 0) {
        perror("[ERROR] Création socket channel impossible\n");
        exit(ERR_CREATE_CHANNEL_SOCKET);
    }

    server_addr.sin_family = sock_domain;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERROR] Liaison socket channel impossible\n");
        close(server_socket);
        exit(ERR_BIND_CHANNEL_SOCKET);
    }

    if (listen(server_socket, max_channels) < 0) {
        perror("[ERROR] Ecoute channel impossible\n");
        close(server_socket);
        exit(ERR_LISTEN_CHANNEL_SOCKET);
    }

    printf("[INFO] %s channel disponible à %s:%d\n", name, ip, port);

    while(off_server == 0) {
        int* client_socket = (int*)malloc(sizeof(int));
        if (client_socket == NULL) {
            perror("[ERROR] Allocation socket client sur channel impossible\n");
            exit(EXIT_FAILURE);
        }

        (*client_socket) = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if ((*client_socket) < 0 && off_server == 0) {
            perror("[ERROR] Connexion client sur le channel impossible\n");
            continue; 
        }

        pthread_mutex_lock(&clients_mutex);
        printf("[INFO] Nouveau client connecté sur %s\n", name);
        
        if (clients_count < clients_curr_size) {
            Client* new_client = (Client*)malloc(sizeof(Client));
            (*new_client).socket = (*client_socket);
            (*new_client).channel_name = name;

            if (add_client(&clients, (*client_socket)) < 0) {
                write((*client_socket), exit_msg, strlen(exit_msg) + 1);
                close((*client_socket));
                free(new_client);
                perror("[ERROR] Ajout client impossible\n");
            } else {
                pthread_t client_thread; 
                pthread_create(&client_thread, NULL, (void*)handle_client, (void*)new_client);
                pthread_detach(client_thread);
            }
        } else {
            write((*client_socket), exit_msg, strlen(exit_msg) + 1);
            printf("[ERROR] Taille stockage clients insuffisante\n");
            close((*client_socket));
        }

        pthread_mutex_unlock(&clients_mutex);
        free(client_socket);
    }

    free(clients);
}

void send_acquit(int *socket) {
    if (write((*socket), acquit_msg, strlen(acquit_msg) + 1) < 0) {
        perror("[ERROR] Envoi acquittement impossible\n");
        //close((*socket));
    } else { printf("[INFO] Envoi acquittement\n"); }
}

void wait_acquit(int* socket) {
    char acquit[4];
    while (strcmp(acquit, "ACK") != 0 && off_server == 0) {
        if (read((*socket), acquit, 4) < 0) {
            close((*socket));
            perror("[ERROR] Lecture acquittement impossible\n");
            exit(ERR_READ_ACK);
        }
    }
    strncpy(acquit, "", sizeof(acquit));
}







