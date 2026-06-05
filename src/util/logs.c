#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "mr_common.h"

static mr_t global_mr = NULL;

void set_log_mr(mr_t mr) {
    global_mr = mr;
}

void mr_log(const char* message){
    if (global_mr == NULL) return; 
    
    char* process = "UNKNOWN";
    if(getpid() == global_mr->mapper) process = "MAPPER";
    else if(getpid() == global_mr->reducer) process = "REDUCER";
    else if(getpid() == global_mr->main) process = "MAIN";

    write_to_log(global_mr->config.log_file, process, message, 0);
}

void mr_err(const char* message){
    if (global_mr == NULL) return; 
    
    char* process = "UNKNOWN";
    if(getpid() == global_mr->mapper) process = "MAPPER";
    else if(getpid() == global_mr->reducer) process = "REDUCER";
    else if(getpid() == global_mr->main) process = "MAIN";

    write_to_log(global_mr->config.log_file, process, message, 1);
}

#if DEBUG
void mr_log_debug(const char* message){
    if (global_mr == NULL) return; 
    
    char* process = "UNKNOWN";
    if(getpid() == global_mr->mapper) process = "MAPPER";
    else if(getpid() == global_mr->reducer) process = "REDUCER";
    else if(getpid() == global_mr->main) process = "MAIN";

    write_to_log(global_mr->config.log_file, process, message, 2);
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


void write_to_log(const char* log_filename, const char* process_name, const char* message, int log_type) {
    if (log_filename == NULL || message == NULL || process_name == NULL) return;

    // Aggiungiamo "/logs/" al filepath
    const char* prefix = "logs/";
    size_t new_len = strlen(prefix) + strlen(log_filename) + 1;
    char* full_path = malloc(new_len);
    if(full_path == NULL) return;

    strcpy(full_path, prefix);
    strcat(full_path, log_filename);

    // Apre il file in modalità append
    FILE *log_file = fopen(full_path, "a");
    if (log_file == NULL) {
        free(full_path);
        return;
    }

    // Setup della struttura per il File Lock
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;  // Write lock (blocca altri scrittori)
    fl.l_whence = SEEK_SET; // Blocca dall'inizio (byte 0) del file
    fl.l_start  = 0;
    fl.l_len    = 0;        // 0 significa bloccare l'intero file

    // Richiede il lock in modo bloccante (attende se un altro processo sta scrivendo)
    fcntl(fileno(log_file), F_SETLKW, &fl);

    // Otteniamo il tempo corrente
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    // Sequenze di escape ANSI per i colori sul terminale (e spesso supportati dai visualizzatori di file di log)
    const char* color_red = "\x1B[31m";
    const char* color_yellow = "\x1B[33m";
    const char* color_reset = "\x1B[0m";

    if (log_type == 1) { // ERROR
        fprintf(log_file, "%s[%s] [%s] ERROR: %s%s\n", color_red, time_str, process_name, message, color_reset);
        fprintf(stderr, "%s[%s] [%s] ERROR: %s%s\n", color_red, time_str, process_name, message, color_reset);
    } else if (log_type == 2) { // DEBUG
        fprintf(log_file, "%s[%s] [%s] DEBUG: %s%s\n", color_yellow, time_str, process_name, message, color_reset);
        fprintf(stderr, "%s[%s] [%s] DEBUG: %s%s\n", color_yellow, time_str, process_name, message, color_reset);
    } else { // INFO
        fprintf(log_file, "[%s] [%s] INFO: %s\n", time_str, process_name, message);
    }
    
    fflush(log_file); // Forza la scrittura fisica su disco 

    // Rilascia il lock
    fl.l_type = F_UNLCK;
    fcntl(fileno(log_file), F_SETLK, &fl);

    // Chiude il descrittore
    fclose(log_file);
    free(full_path);
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