#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

#define MAX_CLIENTS 10
pid_t clients[MAX_CLIENTS];
int client_count = 0;
volatile sig_atomic_t signal_received = 0;
volatile sig_atomic_t signal_type = 0;
volatile sig_atomic_t sender_pid = 0;
volatile sig_atomic_t bit_count = 0;
volatile sig_atomic_t current_byte = 0;
volatile sig_atomic_t ack_received = 0;
// Masti de semnale pentru blocare si asteptare
sigset_t block_mask, empty_mask;
// Blocheaza toate semnalele pentru a proteja sectiunile critice
void block_signals()
{
    sigprocmask(SIG_BLOCK, &block_mask, NULL);
}
// Restaureaza gestionarea semnalelor
void unblock_signals()
{
    sigprocmask(SIG_UNBLOCK, &block_mask, NULL);
}
// Trimite un bit cu protectie impotriva pierderii semnalelor
void send_bit_to_client(pid_t client_pid, int bit)
{
    int sig = bit ? SIGUSR2 : SIGUSR1;
    int max_retries = 3;
    int retry = 0;
    while (retry < max_retries)
    {
        if (kill(client_pid, sig) == -1)
        {
            if (errno == ESRCH)
            {
                // Clientul nu mai exista
                return;
            }
            perror("kill");
            retry++;
            usleep(5000); // Asteapta 5ms inainte de reincercare
        }
        else
        {
            // Asteapta confirmare
            ack_received = 0;

            struct timespec timeout;
            timeout.tv_sec = 0;
            timeout.tv_nsec = 10000000; // 10ms

            sigset_t wait_mask = empty_mask;
            sigaddset(&wait_mask, SIGRTMIN + 1); // Semnal de confirmare (ACK)

            // Asteapta confirmare cu timeout
            if (sigtimedwait(&wait_mask, NULL, &timeout) == -1)
            {
                if (errno == EAGAIN)
                {
                    // Timeout, reincearca
                    retry++;
                    continue;
                }
            }
            else
            {
                // Confirmare primita
                break;
            }
        }
    }

    // Intarziere intre biti pentru a preveni pierderea semnalelor
    usleep(1000);
}
// Trimite un byte complet catre un client
void send_byte_to_client(pid_t client_pid, char byte)
{
    for (int i = 0; i < 8; i++)
    {
        send_bit_to_client(client_pid, (byte >> i) & 1);
    }
}

void remove_client(pid_t pid)
{
    block_signals();
    int found = 0;
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i] == pid)
        {
            found = 1;
        }
        // Muta toti clientii cu o pozitie inapoi dupa cel eliminat
        if (found && i < client_count - 1)
        {
            clients[i] = clients[i + 1];
        }
    }

    if (found)
    {
        client_count--;
        printf("Eliminat client PID: %d, clienti ramasi: %d\n", pid, client_count);

        // Daca nu mai sunt clienti, termina serverul
        if (client_count == 0)
        {
            printf("Nu mai sunt clienti, serverul se termina...\n");
            unblock_signals();
            exit(EXIT_SUCCESS);
        }
    }

    unblock_signals();
}

void add_client(pid_t pid)
{
    block_signals();
    // Verifica daca exista clienti deconectati
    for (int i = 0; i < client_count; i++)
    {
        if (kill(clients[i], 0) == -1 && errno == ESRCH)
        {
            remove_client(clients[i]);
            i--; // Re-verifica aceasta pozitie
        }
    }

    // Verifica daca clientul exista deja
    for (int i = 0; i < client_count; i++)
    {
        if (clients[i] == pid)
        {
            printf("Client PID %d deja inregistrat\n", pid);
            unblock_signals();
            return;
        }
    }

    if (client_count < MAX_CLIENTS)
    {
        clients[client_count++] = pid;
        printf("Adaugat client PID: %d, total clienti: %d\n", pid, client_count);

        // Trimite confirmare catre client
        if (kill(pid, SIGRTMIN + 2) == -1)
        {
            if (errno == ESRCH)
            {
                remove_client(pid);
            }
            else
            {
                perror("kill ack");
            }
        }
    }
    else
    {
        printf("Lista de clienti plina, nu s-a putut adauga PID: %d\n", pid);
    }

    unblock_signals();
}

void broadcast_bit(int bit, pid_t sender_pid)
{
    block_signals();
    for (int i = 0; i < client_count; i++)
    {
        // Nu trimite inapoi expeditorului
        if (clients[i] != sender_pid)
        {
            if (kill(clients[i], 0) == -1)
            {
                if (errno == ESRCH)
                {
                    remove_client(clients[i]);
                    i--; // Re-verifica aceasta pozitie
                    continue;
                }
            }

            send_bit_to_client(clients[i], bit);
        }
    }

    // Trimite confirmare catre expeditor
    if (kill(sender_pid, SIGRTMIN + 3) == -1)
    {
        if (errno == ESRCH)
        {
            remove_client(sender_pid);
        }
    }

    unblock_signals();
}

void handle_signal(int signum, siginfo_t *info, void *context)
{
    (void)context;
    // Nu procesa semnale in handler, doar inregistreaza-le
    signal_received = 1;
    signal_type = signum;
    sender_pid = info->si_pid;

    // Trimite confirmare imediata
    if (kill(info->si_pid, SIGRTMIN + 1) == -1)
    {
        // Nu se poate face mult in handlerul de semnal, doar inregistreaza esecul
        signal_received = 2; // Marcheaza ca eroare
    }
}

void handle_register_signal(int signum, siginfo_t *info, void *context)
{
    (void)signum;
    (void)context;
    add_client(info->si_pid);
}

void handle_ack_signal(int signum, siginfo_t *info, void *context)
{
    (void)signum;
    (void)info;
    (void)context;
    ack_received = 1;
}

int main()
{
    // Initializeaza mastile de semnal
    sigemptyset(&empty_mask);
    sigfillset(&block_mask);
    sigdelset(&block_mask, SIGTERM); // Permite terminarea
    sigdelset(&block_mask, SIGINT);  // Permite Ctrl+C
    // Blocheaza semnalele in timpul initializarii
    block_signals();

    struct sigaction sa, sa_register, sa_ack;
    memset(&sa, 0, sizeof(sa));
    memset(&sa_register, 0, sizeof(sa_register));
    memset(&sa_ack, 0, sizeof(sa_ack));

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handle_signal;
    sigemptyset(&sa.sa_mask);

    sa_register.sa_flags = SA_SIGINFO;
    sa_register.sa_sigaction = handle_register_signal;
    sigemptyset(&sa_register.sa_mask);

    sa_ack.sa_flags = SA_SIGINFO;
    sa_ack.sa_sigaction = handle_ack_signal;
    sigemptyset(&sa_ack.sa_mask);

    if (sigaction(SIGUSR1, &sa, NULL) == -1 ||
        sigaction(SIGUSR2, &sa, NULL) == -1 ||
        sigaction(SIGRTMIN, &sa_register, NULL) == -1 ||
        sigaction(SIGRTMIN + 1, &sa_ack, NULL) == -1 ||
        sigaction(SIGRTMIN + 2, &sa_ack, NULL) == -1 ||
        sigaction(SIGRTMIN + 3, &sa_ack, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    printf("Server PID: %d\n", getpid());

    // Acum deblocheaza semnalele
    unblock_signals();

    while (1)
    {
        // Asteapta un semnal
        sigsuspend(&empty_mask);

        // Proceseaza semnalul primit
        if (signal_received == 1)
        {
            // Determina ce bit a fost primit (0 sau 1)
            int bit = (signal_type == SIGUSR2) ? 1 : 0;

            // Redirectioneaza bitul catre toti clientii, cu exceptia expeditorului
            broadcast_bit(bit, sender_pid);

            signal_received = 0;
        }

        // Verifica daca exista clienti deconectati
        block_signals();
        for (int i = 0; i < client_count; i++)
        {
            if (kill(clients[i], 0) == -1 && errno == ESRCH)
            {
                remove_client(clients[i]);
                i--;
            }
        }
        unblock_signals();
    }

    return 0;
}
