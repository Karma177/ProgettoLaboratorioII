#include "../include/mr.h"
#include <sys/types.h>

struct mr {
    mr_attr_t config;     // Una copia locale della configurazione (thread, code, log)
    mr_mapper_t mapper_f;   // Il puntatore alla funzione Mapper dell'utente
    mr_reducer_t reducer_f; // Il puntatore alla funzione Reducer dell'utente
    void *user_arg;       // L'argomento opzionale passato dall'utente
    mr_hash_t hash_f;
    pid_t mapper;
    pid_t reducer;
};

char* get_log_file_attr(mr_attr_t attr);
char* get_log_file_mr(mr_t mapreducer);
