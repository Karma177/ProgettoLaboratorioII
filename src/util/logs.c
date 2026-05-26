#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "mr_common.h"

char* get_log_file_attr(mr_attr_t attr){
    return attr.log_file;
}

char* get_log_file_mr(mr_t mapreducer){
    if(mapreducer == NULL)
        return NULL;
    return mapreducer->config.log_file;
}


void write_to_log(const char* filepath, const char* process_name, const char* message) {
    if (filepath == NULL || message == NULL || process_name == NULL) return;

    // Apre il file in modalità append
    FILE *log_file = fopen(filepath, "a");
    if (log_file == NULL) return;

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

    fprintf(log_file, "[%s] [%s] %s\n", time_str, process_name, message);
    fflush(log_file); // Forza la scrittura fisica su disco 

    // Rilascia il lock
    fl.l_type = F_UNLCK;
    fcntl(fileno(log_file), F_SETLK, &fl);

    // Chiude il descrittore
    fclose(log_file);
}