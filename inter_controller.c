// Livian Essvein 2211667
// Giovana Nogueira 2220372

#include "common.h"
#include "ipc_shmsig.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

// Canal Kernel->IC via SHM+Sinais
static ipc_shmsig_t* ch_k2ic = NULL;

// PID do processo-filho responsável pelo timer (gera IRQ0)
static pid_t tmr_pid = -1;

// Handler de término: encerra o timer filho e finaliza o IC.
static void on_term(int s){
    (void)s;
    if(tmr_pid > 0) kill(tmr_pid, SIGTERM);
    _exit(0);
}

// argv[1] = shm_name (kernel->IC)
// argv[2] = kernel_pid
int main(int argc, char** argv){
    if(argc<3){
        fprintf(stderr, "Uso: %s <shm_name_k2ic> <kernel_pid>\n", argv[0]);
        return 1;
    }
    const char* shm_name = argv[1];
    pid_t kpid = (pid_t)atoi(argv[2]);

    signal(SIGTERM, on_term);

    // Abre canal Kernel->IC
    ch_k2ic = ipc_open(shm_name);
    if(!ch_k2ic){
        perror("ipc_open(kernel->ic)");
        return 2;
    }
    // Configura sinais/peer (Kernel -> IC)
    ipc_config(ch_k2ic, (ipc_peer_t){
        .sig_data  = SIGHUP,     // kernel avisa “tem dado p/ IC”
        .sig_space = SIGWINCH,   // IC pode avisar “tem espaço”
        .peer_pid  = kpid
    });
    // Bloqueia esses sinais (usaremos wait)
    ipc_block_signals(SIGHUP, SIGWINCH);

    // Cria processo-filho que gera interrupções de tempo (IRQ0) a cada 1s
    tmr_pid = fork();
    if(tmr_pid==0){
        for(;;){
            sleep(1);
            kill(kpid, SIGUSR1); // IRQ0
        }
    }

    // Loop principal: recebe msg do kernel via SHM e notifica IRQ1 após 3s
    for(;;){
        icmsg_t m;
        if (ipc_recv(ch_k2ic, &m) == 0 && m.msg_type == MSG_IO_START){
            sleep(3);
            kill(kpid, SIGUSR2); // IRQ1
        }else{
            // fallback defensivo
            usleep(500);
        }
    }

    ipc_close(ch_k2ic);
    return 0;
}