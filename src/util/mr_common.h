#include "../include/mr.h"
#include <sys/types.h>

// MapReduce utils
struct mr {
    mr_attr_t config;     // Una copia locale della configurazione (thread, code, log)
    mr_mapper_t mapper_f;   // Il puntatore alla funzione Mapper dell'utente
    mr_reducer_t reducer_f; // Il puntatore alla funzione Reducer dell'utente
    void *user_arg;       // L'argomento opzionale passato dall'utente
    pid_t mapper;
    pid_t reducer;
    pid_t main;
};
mr_attr_t* mr_attr_setup();
int mr_set_mapper_pid(mr_t mr, pid_t pid);
int mr_set_reducer_pid(mr_t mr, pid_t pid);
int mr_set_main_pid(mr_t mr, pid_t pid);


// Logs
char* get_log_file_attr(mr_attr_t attr);
char* get_log_file_mr(mr_t mapreducer);
void write_to_log(const char* filepath, const char* process_name, const char* message) {
