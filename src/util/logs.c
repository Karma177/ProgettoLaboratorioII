#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <threads.h>
#include "mr_common.h"
#include <sys/stat.h>
#include <sys/types.h>

static thread_local mr_t global_mr = NULL;

void set_log_mr(mr_t mr) {
    global_mr = mr;
}

void mr_log(const char* message){
    if (global_mr == NULL) return; 
    
    char* process = "UNKNOWN";
    if(getpid() == global_mr->mapper) process = "MAPPER";
    else if(getpid() == global_mr->reducer) process = "REDUCER";
    else if(getpid() == global_mr->main) process = "MAIN";

    write_to_log(global_mr->config.log_file, process, "INFO", message, 0);
}

void mr_err(const char* message){
    if (global_mr == NULL) return; 
    
    char* process = "UNKNOWN";
    if(getpid() == global_mr->mapper) process = "MAPPER";
    else if(getpid() == global_mr->reducer) process = "REDUCER";
    else if(getpid() == global_mr->main) process = "MAIN";

    write_to_log(global_mr->config.log_file, process, "ERROR", message, 1);
}

void mr_log_event(const char* event_name, const char* message){
    if (global_mr == NULL) return; 
    
    char* process = "UNKNOWN";
    if(getpid() == global_mr->mapper) process = "MAPPER";
    else if(getpid() == global_mr->reducer) process = "REDUCER";
    else if(getpid() == global_mr->main) process = "MAIN";

    write_to_log(global_mr->config.log_file, process, event_name, message, 0);
}

void mr_err_event(const char* event_name, const char* message){
    if (global_mr == NULL) return; 
    
    char* process = "UNKNOWN";
    if(getpid() == global_mr->mapper) process = "MAPPER";
    else if(getpid() == global_mr->reducer) process = "REDUCER";
    else if(getpid() == global_mr->main) process = "MAIN";

    write_to_log(global_mr->config.log_file, process, event_name, message, 1);
}

#if DEBUG
void mr_log_debug(const char* message){
    if (global_mr == NULL) return; 
    
    char* process = "UNKNOWN";
    if(getpid() == global_mr->mapper) process = "MAPPER";
    else if(getpid() == global_mr->reducer) process = "REDUCER";
    else if(getpid() == global_mr->main) process = "MAIN";

    write_to_log(global_mr->config.log_file, process, "DEBUG", message, 2);
}
#endif

const char* get_log_file_attr(mr_attr_t attr){
    return attr.log_file;
}

const char* get_log_file_mr(mr_t mapreducer){
    if(mapreducer == NULL)
        return NULL;
    return mapreducer->config.log_file;
}

static char* resolve_log_path(const char* log_filename) {
    char exe_path[1024];
    char dir_path[2048];
    char* full_path = NULL;

    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char* last_slash = strrchr(exe_path, '/');
        if (last_slash != NULL) {
            last_slash[1] = '\0'; // Mantieni solo il path della cartella con lo slash finale
            
            // Se la cartella dell'eseguibile finisce con "dist/", sali di un livello
            size_t path_len = strlen(exe_path);
            if (path_len >= 5 && strcmp(exe_path + path_len - 5, "dist/") == 0) {
                exe_path[path_len - 5] = '\0';
            }

            // Crea la cartella "logs" se non esiste nella cartella di base
            snprintf(dir_path, sizeof(dir_path), "%slogs", exe_path);
            struct stat st = {0};
            if (stat(dir_path, &st) == -1) {
                mkdir(dir_path, 0777);
            }

            size_t new_len = strlen(exe_path) + strlen("logs/") + strlen(log_filename) + 1;
            full_path = malloc(new_len);
            if (full_path) {
                snprintf(full_path, new_len, "%slogs/%s", exe_path, log_filename);
            }
            return full_path;
        }
    }
    
    // Fallback: crea la cartella "logs" nella directory di lavoro corrente
    struct stat st = {0};
    if (stat("logs", &st) == -1) {
        mkdir("logs", 0777);
    }
    
    size_t new_len = strlen("logs/") + strlen(log_filename) + 1;
    full_path = malloc(new_len);
    if (full_path) {
        snprintf(full_path, new_len, "logs/%s", log_filename);
    }
    return full_path;
}

void write_to_log(const char* log_filename, const char* process_name, const char* event_name, const char* message, int log_type) {
    if (log_filename == NULL || message == NULL || process_name == NULL || event_name == NULL) return;

    char* full_path = resolve_log_path(log_filename);
    if (full_path == NULL) return;

    // Acquisisce il semaforo POSIX per la sincronizzazione (processi e thread)
    if (global_mr != NULL && global_mr->log_sem != NULL) {
        sem_wait(global_mr->log_sem);
    }

    // Apre il file in modalità append
    FILE *log_file = fopen(full_path, "a");
    if (log_file == NULL) {
        if (global_mr != NULL && global_mr->log_sem != NULL) {
            sem_post(global_mr->log_sem);
        }
        free(full_path);
        return;
    }

    // Otteniamo il tempo corrente
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    // Otteniamo il thread ID
    unsigned long thread_id = (unsigned long)thrd_current();

    if (log_type == 1) { // ERROR
        fprintf(log_file, "[%s] [%s] [%lu] [%s] ERROR: %s\n", time_str, process_name, thread_id, event_name, message);
        fprintf(stderr, "[%s] [%s] [%lu] [%s] ERROR: %s\n", time_str, process_name, thread_id, event_name, message);
    } else if (log_type == 2) { // DEBUG
        fprintf(log_file, "[%s] [%s] [%lu] [%s] DEBUG: %s\n", time_str, process_name, thread_id, event_name, message);
        fprintf(stderr, "[%s] [%s] [%lu] [%s] DEBUG: %s\n", time_str, process_name, thread_id, event_name, message);
    } else { // INFO / ALTRI EVENTI
        fprintf(log_file, "[%s] [%s] [%lu] [%s] %s\n", time_str, process_name, thread_id, event_name, message);
    }
    
    fflush(log_file); // Forza la scrittura fisica su disco 

    // Chiude il descrittore e libera il path
    fclose(log_file);
    free(full_path);

    // Rilascia il semaforo POSIX
    if (global_mr != NULL && global_mr->log_sem != NULL) {
        sem_post(global_mr->log_sem);
    }
}

char* generate_log_header(){
    time_t now = time(NULL);
    struct tm tm_now;
    char timestr[64];

    if (now == (time_t)-1) return NULL;
    localtime_r(&now, &tm_now);
    if (strftime(timestr, sizeof(timestr), "%Y-%m-%d_%H-%M-%S", &tm_now) == 0) return NULL;

    size_t len = strlen(timestr) + strlen(".log") + 1;
    char *logfile = malloc(len);
    if (logfile == NULL) return NULL;

    strcpy(logfile, timestr);
    strcat(logfile, ".log");
    
    return logfile;
}