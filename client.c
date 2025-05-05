#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#define MAX_BUFFER_SIZE 1024
pid_t server_pid;
volatile sig_atomic_t current_byte = 0;
volatile sig_atomic_t bit_count = 0;
volatile sig_atomic_t connected = 0;
volatile sig_atomic_t ack_received = 0;
// Masti de semnale pentru blocare si asteptare
sigset_t block_mask, empty_mask;
// Blocheaza toate semnalele pentru a proteja sectiunile critice
void block_signals() {
    sigprocmask(SIG_BLOCK, &block_mask, NULL);
}
// Restaureaza gestionarea semnalelor
void unblock_signals() {
    sigprocmask(SIG_UNBLOCK, &block_mask, NULL);
}
void handle_bit_signal(int signum, siginfo_t *info, void *context) {
(void)context;
// Asigura-te ca semnalul este de la serverul nostru
if (info->si_pid != server_pid) {
    return;
}

// SIGUSR2 este bitul 1, SIGUSR1 este bitul 0
int bit = (signum == SIGUSR2) ? 1 : 0;

// Adauga bitul la byte-ul curent
current_byte |= (bit << bit_count);
bit_count++;

// Daca un byte complet a fost primit, afiseaza-l
if (bit_count == 8) {
    printf("%c", (char)current_byte);
    fflush(stdout);
    current_byte = 0;
    bit_count = 0;
}

// Trimite confirmare catre server
if (kill(server_pid, SIGRTMIN+1) == -1) {
    if (errno != ESRCH) {
        perror("kill ack");
    }
}
}

void handle_connect_ack(int signum, siginfo_t *info, void *context) {
    (void)signum;
    (void)context;
    // Asigura-te ca semnalul este de la serverul nostru
    if (info->si_pid != server_pid) {
        return;
    }
    
    connected = 1;
}
void handle_bit_ack(int signum, siginfo_t *info, void *context) {
(void)signum;
(void)context;
// Asigura-te ca semnalul este de la serverul nostru
if (info->si_pid != server_pid) {
    return;
}

ack_received = 1;
}

// Trimite un bit cu protectie impotriva pierderii semnalelor
void send_bit(int bit) {
    block_signals();
    int sig = bit ? SIGUSR2 : SIGUSR1;
    int max_retries = 3;
    int retry = 0;
    
    while (retry < max_retries) {
        ack_received = 0;
    
        if (kill(server_pid, sig) == -1) {
            if (errno == ESRCH) {
                fprintf(stderr, "Serverul nu mai exista\n");
                unblock_signals();
                exit(EXIT_FAILURE);
            }
            perror("kill");
            retry++;
        } else {
            // Asteapta confirmare
            struct timespec timeout;
            timeout.tv_sec = 0;
            timeout.tv_nsec = 10000000; // 10ms
    
            sigset_t wait_mask = empty_mask;
            sigaddset(&wait_mask, SIGRTMIN+3); // Semnal de confirmare bit (ACK)
    
            // Asteapta confirmare cu timeout
            if (sigtimedwait(&wait_mask, NULL, &timeout) == -1) {
                if (errno == EAGAIN) {
                    // Timeout, reincearca
                    retry++;
                    continue;
                }
            } else {
                // Confirmare primita
                break;
            }
        }
    }
    
    if (retry >= max_retries) {
        fprintf(stderr, "Esec la trimiterea bitului dupa %d reincercari\n", max_retries);
    }
    
    // Adauga intarziere intre biti pentru a preveni pierderea semnalelor
    
    unblock_signals();
}

void send_byte(char byte) {
    for (int i = 0; i < 8; i++) {
    send_bit((byte >> i) & 1);
    }
    }
    int main(int argc, char *argv[]) {
    if (argc != 2) {
    fprintf(stderr, "Utilizare: %s <server_pid>\n", argv[0]);
    exit(EXIT_FAILURE);
    }
    server_pid = (pid_t)atoi(argv[1]);

// Initializeaza mastile de semnal
sigemptyset(&empty_mask);
sigfillset(&block_mask);
sigdelset(&block_mask, SIGTERM); // Permite terminarea
sigdelset(&block_mask, SIGINT);  // Permite Ctrl+C

// Blocheaza semnalele in timpul initializarii
block_signals();

printf("Client PID: %d, conectare la server: %d\n", getpid(), server_pid);

struct sigaction sa_bit, sa_connect, sa_ack;
memset(&sa_bit, 0, sizeof(sa_bit));
memset(&sa_connect, 0, sizeof(sa_connect));
memset(&sa_ack, 0, sizeof(sa_ack));

sa_bit.sa_flags = SA_SIGINFO;
sa_bit.sa_sigaction = handle_bit_signal;
sigemptyset(&sa_bit.sa_mask);

sa_connect.sa_flags = SA_SIGINFO;
sa_connect.sa_sigaction = handle_connect_ack;
sigemptyset(&sa_connect.sa_mask);

sa_ack.sa_flags = SA_SIGINFO;
sa_ack.sa_sigaction = handle_bit_ack;
sigemptyset(&sa_ack.sa_mask);

if (sigaction(SIGUSR1, &sa_bit, NULL) == -1 ||
    sigaction(SIGUSR2, &sa_bit, NULL) == -1 ||
    sigaction(SIGRTMIN+1, &sa_ack, NULL) == -1 ||
    sigaction(SIGRTMIN+2, &sa_connect, NULL) == -1 ||
    sigaction(SIGRTMIN+3, &sa_ack, NULL) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
}

// Incearca sa te conectezi la server
if (kill(server_pid, SIGRTMIN) == -1) {
    if (errno == ESRCH) {
        fprintf(stderr, "Server PID %d nu exista\n", server_pid);
    } else {
        perror("kill register");
    }
    exit(EXIT_FAILURE);
}

// Acum deblocheaza semnalele
unblock_signals();

// Asteapta confirmarea conexiunii
struct timespec timeout;
timeout.tv_sec = 1;
timeout.tv_nsec = 0;

sigset_t wait_mask = empty_mask;
sigaddset(&wait_mask, SIGRTMIN+2); // Semnal de confirmare conexiune (ACK)

if (sigtimedwait(&wait_mask, NULL, &timeout) == -1) {
    if (errno == EAGAIN) {
        fprintf(stderr, "Timeout asteptand conexiunea la server\n");
        exit(EXIT_FAILURE);
    }
} else {
    connected = 1;
    printf("Conectat la server cu succes\n");
}

char buffer[MAX_BUFFER_SIZE];
fd_set readfds;
struct timeval tv;

while (1) {
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    // Seteaza timeout pentru select
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100ms

    int ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

    if (ret == -1 && errno != EINTR) {
        perror("select");
        exit(EXIT_FAILURE);
    }

    if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
        // Intrare disponibila
        ssize_t nread = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);

        if (nread == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("read");
                exit(EXIT_FAILURE);
            }
        }

        if (nread > 0) {
            // Proceseaza si trimite fiecare byte
            for (int i = 0; i < nread; i++) {
                send_byte(buffer[i]);
            }
        }
    }

    // Verifica daca serverul mai este activ
    if (kill(server_pid, 0) == -1 && errno == ESRCH) {
        fprintf(stderr, "Serverul s-a terminat\n");
        exit(EXIT_FAILURE);
    }
}

return EXIT_SUCCESS;
}