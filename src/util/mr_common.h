#include "../../include/mr.h"
#include <sys/types.h>
#include <semaphore.h>

// MapReduce utils
#define LIMITE_RAGIONEVOLE (4 * 1024 * 1024)
struct mr {
    mr_attr_t config;     // Una copia locale della configurazione (thread, code, log)
    mr_mapper_t mapper_f;   // Il puntatore alla funzione Mapper dell'utente
    mr_reducer_t reducer_f; // Il puntatore alla funzione Reducer dell'utente
    void *user_arg;       // L'argomento opzionale passato dall'utente
    pid_t mapper;
    pid_t reducer;
    pid_t main;
    sem_t *log_sem;       // Semaforo per la sincronizzazione dei log
};
mr_attr_t* mr_attr_setup(size_t mapthreads, size_t reducerthreads, size_t queuesize, char* logfile);
int mr_set_mapper_pid(mr_t mr, pid_t pid);
int mr_set_reducer_pid(mr_t mr, pid_t pid);
int mr_set_main_pid(mr_t mr, pid_t pid);


// Logs
#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG
#define mr_debug(msg) mr_log_debug(msg)
void mr_log_debug(const char* message);
#else
#define mr_debug(msg) do { } while (0)
#endif

char* generate_log_header();
const char* get_log_file_attr(mr_attr_t attr);
const char* get_log_file_mr(mr_t mapreducer);
void write_to_log(const char* filepath, const char* process_name, const char* event_name, const char* message, int log_type);
void mr_log(const char* message);
void mr_err(const char* message);
void mr_log_event(const char* event_name, const char* message);
void mr_err_event(const char* event_name, const char* message);
void set_log_mr(mr_t mr);

// Mapper
int start_mapper(mr_t mr);

// Lettura/Scrittura esatta su file descriptor
ssize_t readn(int fd, void *buf, size_t n);
ssize_t writen(int fd, const void *buf, size_t n);