#include <stdio.h>
#include <stdlib.h>
#include "util/mr_common.h"
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>

// TODO: Aggiungere [FATAL] se l'errore è fatale.
int start_reducer(mr_t mr);
int start_main_job(mr_t mr, const char *input_path, const char *output_path, int mapper_write_fd, int reducer_read_fd);
int serialize_and_send(mr_t mr, const char* filepath, int write_fd, unsigned long *line_counter);
int listen_to_reducer(int read_fd, const char* output_path);
int wait_for_others(mr_t mr);

typedef struct filename_node {
    char* path;
    struct filename_node* next;
} filename_node;

filename_node* insert_sorted(filename_node* head, const char* path);
filename_node* scan_directory(const char* dir_path, filename_node* head);

static void close_all_fds_except_std(void) {
    int max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0) max_fd = 1024;
    for (int i = 3; i < max_fd; i++) {
        close(i);
    }
}

int mr_start(mr_t mr, const char *input_path, const char *output_path){
    char log_msg[256];
    if (mr == NULL || input_path == NULL || output_path == NULL) {
        mr_err("Utilizzo 'mr_start' errato (argomenti nulli). Uscendo...");
        return -1;
    }

    // Impostiamo il logger per usare questo mr
    set_log_mr(mr);
    mr_set_main_pid(mr, getpid());

    #if DEBUG
    mr_debug("mr_start: Avvio dell'esecuzione MapReduce nel main process.");
    #endif

    // TODO: proper error handling
    int main_to_mapper[2];
    int mapper_to_reducer[2];
    int reducer_to_main[2];
    
    if(pipe(main_to_mapper) == -1 || pipe(mapper_to_reducer) == -1 || pipe(reducer_to_main) == -1){
        mr_err_event("PIPE_CREATION_FAILED", "Pipe non inizializzate. Uscendo..");
        return -1;
    }
    mr_log_event("PIPE_CREATION", "Pipe di comunicazione create con successo.");

    /*
        MAPPER:
        sezione di codice dedicata al mapper.
    */
    size_t mapper_pid = fork();
    if(mapper_pid == 0){
        // Logica mapper
        set_log_mr(mr);
        mr_set_mapper_pid(mr, getpid());
        mr_log("Inizializzazione mapper");

        // Pipe
        dup2(main_to_mapper[0], STDIN_FILENO);
        dup2(mapper_to_reducer[1], STDOUT_FILENO);
        mr_log("Pipe inizializzate.");

        close_all_fds_except_std();

        int ret; 
        if((ret = start_mapper(mr)) != 0) {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg), "Il processo mapper ha terminato con un errore. Errore: %d", ret);
            mr_err(err_msg);
        }
        exit(ret);
    }
    mr_set_mapper_pid(mr, mapper_pid);
    snprintf(log_msg, sizeof(log_msg), "Processo mapper creato con PID %zu", mapper_pid);
    mr_log_event("PROCESS_CREATION", log_msg);

    /*
        REDUCER:
        sezione di codice dedicata al reducer.
    */
    size_t reducer_pid = fork();
    if(reducer_pid == 0){
        // Logica reducer
        set_log_mr(mr);
        mr_set_reducer_pid(mr, getpid());
        mr_log("Inizializzazione reducer");

        // Pipe
        dup2(mapper_to_reducer[0], STDIN_FILENO);
        dup2(reducer_to_main[1], STDOUT_FILENO);
        mr_log("Pipe inizializzate.");

        close_all_fds_except_std();

        int ret; 
        if((ret = start_reducer(mr)) != 0) {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg), "Il processo reducer ha terminato con un errore. Errore: %d", ret);
            mr_err(err_msg);
        }
        exit(ret);
    }
    mr_set_reducer_pid(mr, reducer_pid);
    snprintf(log_msg, sizeof(log_msg), "Processo reducer creato con PID %zu", reducer_pid);
    mr_log_event("PROCESS_CREATION", log_msg);

    /*
        MAIN PROCESS:
        sezione di codice dedicata al processo principale.
    */
    mr_log("Inizializzazione main process...");

    close(main_to_mapper[0]);
    close(mapper_to_reducer[0]);
    close(mapper_to_reducer[1]);
    close(reducer_to_main[1]);

    // Risolviamo l'output_path relativo alla directory dell'eseguibile se necessario
    char resolved_output_path[1024];
    if (output_path[0] == '/') {
        strncpy(resolved_output_path, output_path, sizeof(resolved_output_path) - 1);
        resolved_output_path[sizeof(resolved_output_path) - 1] = '\0';
    } else {
        char exe_path[1024];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len != -1) {
            exe_path[len] = '\0';
            char* last_slash = strrchr(exe_path, '/');
            if (last_slash != NULL) {
                last_slash[1] = '\0';
                
                // Se la cartella dell'eseguibile finisce con "dist/", sali di un livello
                size_t path_len = strlen(exe_path);
                if (path_len >= 5 && strcmp(exe_path + path_len - 5, "dist/") == 0) {
                    exe_path[path_len - 5] = '\0';
                }

                snprintf(resolved_output_path, sizeof(resolved_output_path), "%s%s", exe_path, output_path);
            } else {
                strncpy(resolved_output_path, output_path, sizeof(resolved_output_path) - 1);
                resolved_output_path[sizeof(resolved_output_path) - 1] = '\0';
            }
        } else {
            strncpy(resolved_output_path, output_path, sizeof(resolved_output_path) - 1);
            resolved_output_path[sizeof(resolved_output_path) - 1] = '\0';
        }
    }

    return start_main_job(mr, input_path, resolved_output_path, main_to_mapper[1], reducer_to_main[0]);
}

// Sezione "Serialize and Send": tutto ciò necessario alla scansione e l'invio dei file al mapper.
int start_main_job(mr_t mr, const char *input_path, const char *output_path, int mapper_write_fd, int reducer_read_fd){
    char log_msg[256];
    struct stat path_stat;
    if (stat(input_path, &path_stat) != 0) {
        mr_err("Il percorso specificato per l'input non è valido.");
        return -1;
    }
    unsigned long line_n = 0;
    
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    if (S_ISREG(path_stat.st_mode)) {
        mr_log("L'input è un file singolo..");
        serialize_and_send(mr, input_path, mapper_write_fd, &line_n);
    } else if (S_ISDIR(path_stat.st_mode)) {
        mr_log("L'input è una directory..");
        
        filename_node* head = NULL;
        head = scan_directory(input_path, head);

        // Scorriamo la lista ordinata e inviamo i file
        filename_node* curr = head;
        while (curr != NULL) {
            serialize_and_send(mr, curr->path, mapper_write_fd, &line_n);

            // Pulizia della lista in-place
            filename_node* temp = curr;
            curr = curr->next;
            free(temp->path);
            free(temp);
        } 
    } else {
        mr_log("Input non valido. Non è né directory né file.");
        return -1;
    }
    close(mapper_write_fd);
    snprintf(log_msg, sizeof(log_msg), "Inviate %lu righe al mapper", line_n);
    mr_log_event("METRIC_LINES", log_msg);
    listen_to_reducer(reducer_read_fd, output_path);
    close(reducer_read_fd);
    wait_for_others(mr);
    
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    FILE *f_out = fopen(output_path, "a");
    if (f_out) {
        fprintf(f_out, "\n--- Statistiche di esecuzione ---\n");
        fprintf(f_out, "Righe processate dal Main process: %lu\n", line_n);
        fprintf(f_out, "Tempo di esecuzione: %.4f secondi\n", elapsed);
        fprintf(f_out, "Thread utilizzati: %zu mapper, %zu reducer\n", mr->config.mapper_threads, mr->config.reducer_threads);
        fprintf(f_out, "Logfile: %s\n", mr->config.log_file);
        fprintf(f_out, "Queue size: %zu\n", mr->config.queue_size);
        fprintf(f_out, "Input path: %s\n", input_path);
        fprintf(f_out, "Output path: %s\n", output_path );
        fclose(f_out);
    }

    mr_destroy(mr);
    return 0;
}

// Inserimento ordinato (lessicografico)
filename_node* insert_sorted(filename_node* head, const char* path) {
    filename_node* new_node = malloc(sizeof(filename_node));
    new_node->path = strdup(path);
    new_node->next = NULL;

    // Se la lista è vuota o il nuovo elemento va prima della testa
    if (head == NULL || strcmp(head->path, path) > 0) {
        new_node->next = head;
        return new_node;
    }

    // Troviamo il punto di inserimento
    filename_node* curr = head;
    while (curr->next != NULL && strcmp(curr->next->path, path) < 0) {
        curr = curr->next;
    }

    new_node->next = curr->next;
    curr->next = new_node;
    return head;
}

// Scansione ricorsiva delle subdirectories
filename_node* scan_directory(const char* dir_path, filename_node* head) {
    DIR* dir = opendir(dir_path);
    if (dir == NULL) {
        mr_err("Impossibile aprire la directory.");
        return head;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Ignora . e ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat entry_stat;
        if (stat(full_path, &entry_stat) == 0) {
            if (S_ISDIR(entry_stat.st_mode)) {
                // Ricorsione per le sottodirectory!
                head = scan_directory(full_path, head);
            } else if (S_ISREG(entry_stat.st_mode)) {
                // Inserimento ordinato
                head = insert_sorted(head, full_path);
            }
        }
    }
    closedir(dir);
    return head;
}

int serialize_and_send(mr_t mr, const char* filepath, int write_fd, unsigned long *line_counter) {
    (void)mr;
    char log_msg[512];
    snprintf(log_msg, sizeof(log_msg), "Apertura file di input: %s", filepath);
    mr_log_event("FILE_OPEN", log_msg);
    FILE* file = fopen(filepath, "r");
    if (!file) {
        char err_msg[512];
        snprintf(err_msg, sizeof(err_msg), "Impossibile aprire il file per la serializzazione: %s", filepath);
        mr_err_event("FILE_OPEN_FAILED", err_msg);
        return -1;
    }
    
    char* line = NULL;
    size_t len = 0;
    ssize_t read_bytes;
    size_t file_name_len = strlen(filepath);

    // Leggiamo il file riga per riga
    while ((read_bytes = getline(&line, &len, file)) != -1) {
        (*line_counter)++;
        if (read_bytes > 0 && line[read_bytes - 1] == '\n') {
            line[read_bytes - 1] = '\0';
            read_bytes--;
        }
        int line_len = read_bytes;

        // Inviamo la lunghezza del nome del file 
        if (writen(write_fd, &file_name_len, sizeof(size_t)) < 0) break;

        // Inviamo i caratteri del nome del file 
        if (writen(write_fd, filepath, file_name_len) < 0) break;

        // Inviamo il numero della riga 
        size_t current_line_num = *line_counter;
        if (writen(write_fd, &current_line_num, sizeof(size_t)) < 0) break;

        // Inviamo la lunghezza del testo della riga
        if (writen(write_fd, &line_len, sizeof(int)) < 0) break;

        // Inviamo i caratteri effettivi della riga 
        if (writen(write_fd, line, line_len) < 0) break;
    }

    free(line);
    fclose(file);
    snprintf(log_msg, sizeof(log_msg), "Chiuso file di input: %s", filepath);
    mr_log_event("FILE_CLOSE", log_msg);
    return 0;
}

// Sezione di ascolto dal reducer: tutto ciò che è necessario per l'ascolto dal reducer.
int listen_to_reducer(int read_fd, const char* output_path){
    FILE* out_file = NULL;
    char log_msg[512];
    
    if (output_path != NULL) {
        out_file = fopen(output_path, "w");
        if (out_file == NULL) {
            snprintf(log_msg, sizeof(log_msg), "Impossibile aprire il file di output: %s", output_path);
            mr_err_event("FILE_OPEN_FAILED", log_msg);
            return -1;
        }
        snprintf(log_msg, sizeof(log_msg), "Aperto file di output: %s", output_path);
        mr_log_event("FILE_OPEN", log_msg);
    } else {
        out_file = stdout;
    }

    int token_length;
    while(readn(read_fd, &token_length, sizeof(int)) == sizeof(int)){
        // Prendiamo il token
        if(token_length <= 0 || token_length > LIMITE_RAGIONEVOLE){
            errno = EINVAL;
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg), "Lunghezza token non valida ricevuta dal Reducer: %d", token_length);
            mr_err(err_msg);
            if (out_file && out_file != stdout) fclose(out_file);
            return -1;
        }
        char *token = malloc(token_length+1); // +1 per \0
        readn(read_fd, token, token_length);
        token[token_length] = '\0';

        // Prendiamo il risultato
        int result_length;
        readn(read_fd, &result_length, sizeof(int));
        if(result_length <= 0 || result_length > LIMITE_RAGIONEVOLE){
            errno = EINVAL;
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg), "Lunghezza risultato non valida ricevuta dal Reducer: %d", result_length);
            mr_err(err_msg);
            free(token);
            if (out_file && out_file != stdout) fclose(out_file);
            return -1;
        }

        void *result = NULL;
        if (result_length > 0) {
            result = malloc(result_length);
            if (!result) {
                free(token);
                if (out_file && out_file != stdout) fclose(out_file);
                return -1;
            }
            readn(read_fd, result, result_length);
        }

        // Scriviamo sul file di output
        if (out_file) {
            fprintf(out_file, "%s: ", token);
            if (result && result_length > 0) {
                fwrite(result, 1, result_length, out_file);
            }
            fprintf(out_file, "\n");
        }

        free(token);
        if (result) free(result);
    }

    if (out_file != NULL && out_file != stdout) {
        fclose(out_file);
        snprintf(log_msg, sizeof(log_msg), "Chiuso file di output: %s", output_path);
        mr_log_event("FILE_CLOSE", log_msg);
    }
    return 0;
}

// Legge ESATTAMENTE 'n' byte bloccandosi finché non li ha presi tutti (o finché non c'è EOF/errore)
ssize_t readn(int fd, void *buf, size_t n) {
    size_t left = n;
    ssize_t nread;
    char *ptr = buf; // per spostamenti di byte singoli

    while (left > 0) {
        if ((nread = read(fd, ptr, left)) < 0) {
            if (errno == EINTR) nread = 0; // Se interrotto da un segnale, ricomincia
            else return -1;                // Errore vero
        } else if (nread == 0) {
            break; // EOF: la pipe è stata chiusa dall'altra parte
        } else {
            left -= nread;
            ptr += nread;
        }
    }
    return (n - left); // Ritorna quanti byte è riuscito a leggere davvero
}

// Scrive ESATTAMENTE 'n' byte bloccandosi finché non li ha scritti tutti (o finché non c'è errore)
ssize_t writen(int fd, const void *buf, size_t n) {
    size_t left = n;
    ssize_t nwritten;
    const char *ptr = buf;

    while (left > 0) {
        if ((nwritten = write(fd, ptr, left)) <= 0) {
            if (nwritten < 0 && errno == EINTR) {
                nwritten = 0; // Se interrotto da un segnale, ricomincia
            } else {
                return -1; // Errore vero o pipe chiusa
            }
        }
        left -= nwritten;
        ptr += nwritten;
    }
    return n; // Ritorna quanti byte è riuscito a scrivere davvero
}

// Sezione dedicata ai metodi di pulizia e termine del programma
int wait_for_others(mr_t mr){
    waitpid(mr->mapper, NULL, 0);
    waitpid(mr->reducer, NULL, 0);

    return 0;
}
