#ifndef IPC_SHMSIG_H
#define IPC_SHMSIG_H

#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>

// Canal SPSC (um produtor/um consumidor) em memória compartilhada + sinais.
// Sem semáforos, sem pipes.

typedef struct ipc_shmsig ipc_shmsig_t;

typedef struct {
    int   sig_data;   // sinal “tem dado”
    int   sig_space;  // sinal “tem espaço”
    pid_t peer_pid;   // PID do outro lado (para kill/sigqueue)
} ipc_peer_t;

// Criador do canal (dono do shm)
ipc_shmsig_t* ipc_create(const char* name, size_t elem_size, uint32_t capacity);

// Abridor do canal (lado que só usa um shm pré-criado)
ipc_shmsig_t* ipc_open(const char* name);

// Configura sinais + peer
void ipc_config(ipc_shmsig_t* ch, ipc_peer_t conf);

// Envia elemento (bloqueia com sinais se cheio)
int ipc_send(ipc_shmsig_t* ch, const void* elem);

// Recebe elemento (bloqueia com sinais se vazio)
int ipc_recv(ipc_shmsig_t* ch, void* out_elem);

// Fecha mapeamento/FD
void ipc_close(ipc_shmsig_t* ch);

// Remove SHM (quem criou)
int ipc_unlink(const char* name);

// Util: bloquear sinais que serão usados com sigwaitinfo/sigsuspend
int ipc_block_signals(int sig_a, int sig_b);

#endif