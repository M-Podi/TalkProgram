# Sistem Talk Multi-Client cu Semnale UNIX

## Descriere

Această implementare realizează un sistem de chat (talk) între mai multe persoane folosind exclusiv semnale UNIX pentru comunicarea inter-proces. Sistemul este compus din două programe: un **server** care gestionează comunicarea și mai mulți **clienți** care se conectează pentru a participa la conversație.

### Utilizarea

#### 1. Compilarea

```bash
$ make all
```

#### 2. Pornirea Serverului
```bash
$ ./server
Server PID: 12345
```
Serverul va afișa PID-ul său și va aștepta conexiuni.

#### 3. Conectarea Clienților
```bash
# Terminal 1
$ ./client 12345
Client PID: 12346, conectare la server: 12345
Conectat la server cu succes

# Terminal 2  
$ ./client 12345
Client PID: 12347, conectare la server: 12345
Conectat la server cu succes
```


## Respectarea Cerințelor

### Transmisia pe Biți
- **Cerință**: "fiecare byte de informație care trebuie trimis este scris pe 8 biți"
- **Implementare**: 
```c
void send_byte_to_client(pid_t client_pid, char byte)
{
    for (int i = 0; i < 8; i++) {
        send_bit_to_client(client_pid, (byte >> i) & 1);
    }
}
```

### Protecția la Pierderea Semnalelor
- **Cerință**: "se va asigura protecția la pierderea de semnale"
- **Implementare**: Mecanism de retry cu timeout și confirmare (ACK)

### Argumentul PID în Linia de Comandă
- **Cerință**: "clientul... căruia îi va transmite ca argument în linia de comandă PID-ul serverului"
- **Implementare**:
```c
if (argc != 2) {
    fprintf(stderr, "Utilizare: %s <server_pid>\n", argv[0]);
    exit(EXIT_FAILURE);
}
server_pid = (pid_t)atoi(argv[1]);
```

### Utilizarea SA_SIGINFO
- **Cerință**: "serverul își instalează handlerele cu 'SA_SIGINFO'"
- **Implementare**:
```c
sa.sa_flags = SA_SIGINFO;
sa.sa_sigaction = handle_signal;
```

### Gestionarea Listei de Clienți
- **Cerință**: "pe măsură ce clienți noi se conectează, el ține într-o listă PID-urile acestora"
- **Implementare**: Array `clients[MAX_CLIENTS]` cu funcții `add_client()` și `remove_client()`

### Eliminarea PID-urilor Invalide
- **Cerință**: "dacă vreun PID devine invalid (eroarea 'ESRCH' pentru 'kill()'), îl elimină din listă"
- **Implementare**:
```c
if (kill(clients[i], 0) == -1 && errno == ESRCH) {
    remove_client(clients[i]);
}
```

### Terminarea Serverului
- **Cerință**: "când lista redevine vidă, procesul server se termină"
- **Implementare**:
```c
if (client_count == 0) {
    printf("Nu mai sunt clienți, serverul se termină...\n");
    exit(EXIT_SUCCESS);
}
```
