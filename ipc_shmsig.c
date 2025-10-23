#define _GNU_SOURCE
#include "ipc_shmsig.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

typedef struct {
    uint32_t capacity;
    uint32_t elem_size;
    _Atomic uint32_t head;     // leitura
    _Atomic uint32_t tail;     // escrita
    _Atomic int has_space;     // escritor aguardando espaço
    _Atomic int has_data;      // leitor aguardando dado
} ring_meta_t;

typedef struct {
    ring_meta_t meta;
    unsigned char data[];
} ring_t;

struct ipc_shmsig {
    int fd;
    size_t map_len;
    ring_t* ring;
    int sig_data;
    int sig_space;
    pid_t peer_pid;
    char name[64];
};

static size_t calc_len(uint32_t cap, uint32_t esz){
    return sizeof(ring_t) + (size_t)cap * esz;
}
static inline uint32_t next_idx(uint32_t x, uint32_t cap){
    return (x+1u==cap)?0u:(x+1u);
}

int ipc_block_signals(int sig_a, int sig_b){
    sigset_t set;
    sigemptyset(&set);
    if(sig_a>0) sigaddset(&set, sig_a);
    if(sig_b>0 && sig_b!=sig_a) sigaddset(&set, sig_b);
    return pthread_sigmask(SIG_BLOCK, &set, NULL);
}

static int wait_one(int signo){
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, signo);
    int got = 0;
    // sigwait returns 0 on success; it does not use errno
    int r = sigwait(&set, &got);
    return (r == 0) ? 0 : -1;
}

ipc_shmsig_t* ipc_create(const char* name, size_t elem_size, uint32_t capacity){
    ipc_shmsig_t* ch = calloc(1,sizeof(*ch));
    if(!ch) return NULL;
    strncpy(ch->name, name, sizeof(ch->name)-1);
    ch->map_len = calc_len(capacity, (uint32_t)elem_size);

    ch->fd = shm_open(name, O_CREAT|O_RDWR, 0600);
    if(ch->fd<0){ free(ch); return NULL; }
    if(ftruncate(ch->fd, ch->map_len)!=0){
        close(ch->fd); shm_unlink(name); free(ch); return NULL;
    }
    void* p = mmap(NULL, ch->map_len, PROT_READ|PROT_WRITE, MAP_SHARED, ch->fd, 0);
    if(p==MAP_FAILED){
        close(ch->fd); shm_unlink(name); free(ch); return NULL;
    }
    ch->ring = (ring_t*)p;
    ch->ring->meta.capacity = capacity;
    ch->ring->meta.elem_size = (uint32_t)elem_size;
    atomic_store(&ch->ring->meta.head, 0);
    atomic_store(&ch->ring->meta.tail, 0);
    atomic_store(&ch->ring->meta.has_space, 0);
    atomic_store(&ch->ring->meta.has_data, 0);
    return ch;
}

ipc_shmsig_t* ipc_open(const char* name){
    ipc_shmsig_t* ch = calloc(1,sizeof(*ch));
    if(!ch) return NULL;
    strncpy(ch->name, name, sizeof(ch->name)-1);
    ch->fd = shm_open(name, O_RDWR, 0600);
    if(ch->fd<0){ free(ch); return NULL; }
    struct stat st;
    if(fstat(ch->fd, &st)!=0){ close(ch->fd); free(ch); return NULL; }
    ch->map_len = (size_t)st.st_size;
    void* p = mmap(NULL, ch->map_len, PROT_READ|PROT_WRITE, MAP_SHARED, ch->fd, 0);
    if(p==MAP_FAILED){ close(ch->fd); free(ch); return NULL; }
    ch->ring = (ring_t*)p;
    return ch;
}

void ipc_config(ipc_shmsig_t* ch, ipc_peer_t conf){
    ch->sig_data  = conf.sig_data;
    ch->sig_space = conf.sig_space;
    ch->peer_pid  = conf.peer_pid;
}

int ipc_send(ipc_shmsig_t* ch, const void* elem){
    ring_t* R = ch->ring;
    const uint32_t cap = R->meta.capacity;
    const uint32_t esz = R->meta.elem_size;

    for(;;){
        uint32_t head = atomic_load_explicit(&R->meta.head, memory_order_acquire);
        uint32_t tail = atomic_load_explicit(&R->meta.tail, memory_order_relaxed);
        uint32_t next = next_idx(tail, cap);

        if(next != head){
            // há espaço
            memcpy(R->data + (size_t)tail * esz, elem, esz);
            atomic_store_explicit(&R->meta.tail, next, memory_order_release);
            // notifica reader
            if(ch->peer_pid>0 && ch->sig_data>0){
                atomic_store(&R->meta.has_data, 1);
                kill(ch->peer_pid, ch->sig_data);
            }
            return 0;
        }
        // cheio: marca interesse e espera sinal de espaço
        atomic_store(&R->meta.has_space, 1);
        if(ch->sig_space>0){
            if(wait_one(ch->sig_space)!=0) return -1;
        } else {
            // fallback macio
            usleep(500);
        }
    }
}

int ipc_recv(ipc_shmsig_t* ch, void* out_elem){
    ring_t* R = ch->ring;
    const uint32_t cap = R->meta.capacity;
    const uint32_t esz = R->meta.elem_size;

    for(;;){
        uint32_t head = atomic_load_explicit(&R->meta.head, memory_order_relaxed);
        uint32_t tail = atomic_load_explicit(&R->meta.tail, memory_order_acquire);

        if(head != tail){
            memcpy(out_elem, R->data + (size_t)head * esz, esz);
            uint32_t next = next_idx(head, cap);
            atomic_store_explicit(&R->meta.head, next, memory_order_release);
            // havia escritores esperando? avisa espaço
            if(atomic_exchange(&R->meta.has_space, 0)>0 && ch->peer_pid>0 && ch->sig_space>0){
                kill(ch->peer_pid, ch->sig_space);
            }
            return 0;
        }
        // vazio: marca interesse e espera sinal de dado
        atomic_store(&R->meta.has_data, 1);
        if(ch->sig_data>0){
            if(wait_one(ch->sig_data)!=0) return -1;
        } else {
            usleep(500);
        }
    }
}

void ipc_close(ipc_shmsig_t* ch){
    if(!ch) return;
    if(ch->ring) munmap(ch->ring, ch->map_len);
    if(ch->fd>=0) close(ch->fd);
    free(ch);
}

int ipc_unlink(const char* name){
    return shm_unlink(name);
}