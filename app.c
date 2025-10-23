// Livian Essvein 2211667
// Giovana Nogueira 2220372

#include "common.h"
#include "ipc_shmsig.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>

// Canal App->Kernel via SHM+Sinais
static ipc_shmsig_t* ch_app2k = NULL;

static char me_name[MAX_NAME];
static int idx = 0;
static pid_t kernel_pid = -1;

// Envia syscall RW ao kernel e se auto-suspende
static void do_syscall_rw(int rw_flag){
    appmsg_t m = { .msg_type = MSG_SYSCALL_RW, .pid = getpid(), .arg = rw_flag };
    // envia pela SHM (notifica o kernel por SIGALRM)
    ipc_send(ch_app2k, &m);
    // suspende voluntariamente (mantém comportamento original)
    raise(SIGSTOP);
}

// Reporta PC atual ao kernel
static void send_status(int pc){
    appmsg_t m = { .msg_type = MSG_APP_STATUS, .pid = getpid(), .arg = pc };
    ipc_send(ch_app2k, &m);
    // não precisamos enviar SIGALRM manualmente: o canal já sinaliza
}

int main(int argc, char** argv){
    // Novo uso:
    // argv[1] = nome do canal SHM app->kernel (ex: "/app4_to_kernel")
    // argv[2] = nome do processo (ex: "A4")
    // argv[3] = idx
    // argv[4] = kernel_pid
    if(argc<5){
        fprintf(stderr,"Uso: %s <shm_name_app2k> <nome> <idx> <kernel_pid>\n", argv[0]);
        return 1;
    }
    const char* shm_name = argv[1];
    strncpy(me_name, argv[2], sizeof(me_name)-1);
    idx = atoi(argv[3]);
    kernel_pid = (pid_t)atoi(argv[4]);

    // Abre o canal app->kernel (kernel deve tê-lo criado previamente)
    ch_app2k = ipc_open(shm_name);
    if(!ch_app2k){
        perror("ipc_open(app->kernel)");
        return 2;
    }
    // Configura sinais/peer (App -> Kernel)
    ipc_config(ch_app2k, (ipc_peer_t){
        .sig_data  = SIGALRM,   // avisa kernel “tem dado do app”
        .sig_space = SIGCONT,   // kernel pode avisar “tem espaço”
        .peer_pid  = kernel_pid
    });
    // Bloqueia sinais usados para que possamos usar sigwait/sigwaitinfo internamente
    ipc_block_signals(SIGALRM, SIGCONT);

    // Perfil
    enum { PROFILE_SPLIT, PROFILE_CPU, PROFILE_IO } profile_mode = PROFILE_SPLIT;
    const char *env_profile = getenv("APP_PROFILE");
    if(env_profile){
        if(strcmp(env_profile, "cpu")==0) profile_mode = PROFILE_CPU;
        else if(strcmp(env_profile, "io")==0) profile_mode = PROFILE_IO;
        else if(strcmp(env_profile, "split")==0) profile_mode = PROFILE_SPLIT;
    }

    int io_points[5]={0};
    int io_n=0;

    if(profile_mode == PROFILE_CPU){
        io_n = 0;
    } else if(profile_mode == PROFILE_IO){
        if(idx==1){ int tmp[]={3,7,12}; memcpy(io_points,tmp,sizeof(int)*3); io_n=3; }
        else if(idx==2){ int tmp[]={4,9}; memcpy(io_points,tmp,sizeof(int)*2); io_n=2; }
        else if(idx==3){ int tmp[]={5,10}; memcpy(io_points,tmp,sizeof(int)*2); io_n=2; }
        else { int tmp[]={6,11}; memcpy(io_points,tmp,sizeof(int)*2); io_n=2; }
    } else { // split
        if(idx <= 3){
            io_n = 0;
        } else {
            if(idx==4){ int tmp[]={3,7,12}; memcpy(io_points,tmp,sizeof(int)*3); io_n=3; }
            else if(idx==5){ int tmp[]={6,11}; memcpy(io_points,tmp,sizeof(int)*2); io_n=2; }
            else /* idx==6 */{ int tmp[]={5,10}; memcpy(io_points,tmp,sizeof(int)*2); io_n=2; }
        }
    }

    const int MAX = 15;

    for(int pc=1; pc<=MAX; ++pc){
        send_status(pc);
        sleep(1);

        for(int k=0;k<io_n;k++){
            if(pc == io_points[k]){
                do_syscall_rw((pc%2)==0 ? 1 : 0);
                break;
            }
        }
    }

    ipc_close(ch_app2k);
    return 0;
}