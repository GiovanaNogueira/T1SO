CC      = clang
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lpthread

# Binários
BINS = kernel_sim inter_controller app

all: $(BINS) kernel   # 'kernel' é alias de kernel_sim

# Novo módulo de IPC (shm + sinais)
IPC_SRCS = ipc_shmsig.c
IPC_HDRS = ipc_shmsig.h

kernel_sim: kernel_sim.c common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

inter_controller: inter_controller.c common.h $(IPC_HDRS) $(IPC_SRCS)
	$(CC) $(CFLAGS) -o $@ inter_controller.c $(IPC_SRCS) $(LDFLAGS)

app: app.c common.h $(IPC_HDRS) $(IPC_SRCS)
	$(CC) $(CFLAGS) -o $@ app.c $(IPC_SRCS) $(LDFLAGS)

kernel: kernel_sim
	cp -f kernel_sim kernel

clean:
	rm -f $(BINS) kernel *.o saida_*.txt

.PHONY: all clean test_cpu test_io test_all

# ---- Testes do professor ----
test_cpu: kernel app
	@echo "== Teste CPU (A1..A3 sem I/O) =="
	APP_PROFILE=cpu ./kernel 3 | tee saida_cpu.txt

test_io: kernel app
	@echo "== Teste I/O (A4..A6 com I/O) =="
	APP_PROFILE=io APP_NAME_OFFSET=3 ./kernel 3 | tee saida_io.txt

test_all: kernel app
	@echo "== Teste completo (6 apps mistos) =="
	APP_PROFILE=split ./kernel 6 | tee saida_6apps.txt