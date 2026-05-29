#include <stdio.h>
#include <stdlib.h>
#include <util/mr_common.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>


int mr_start(mr_t mr, const char *input_path, const char *output_path){
    if (mr == NULL || input_path == NULL || output_path == NULL) {
        mr_err("Utilizzo 'mr_start' errato (argomenti nulli). Uscendo...");
        return -1;
    }

    // Impostiamo il logger per usare questo mr
    set_log_mr(mr);
    mr_set_main_pid(mr, getpid());

    // TODO: proper error handling
    int main_to_mapper[2];
    int mapper_to_reducer[2];
    int reducer_to_main[2];
    
    if(pipe(main_to_mapper) == -1 || pipe(mapper_to_reducer) == -1 || pipe(reducer_to_main) == -1){
        mr_err("Pipe non inizializzate. Uscendo..");
        return -1;
    }

    size_t mapper_pid = fork();
    if(mapper_pid == 0){
        // Logica mapper
        mr_log("Inizializzazione mapper");

        // Pipe
        dup2(main_to_mapper[0], STDIN_FILENO);
        dup2(mapper_to_reducer[1], STDOUT_FILENO);
        mr_log("Pipe inizializzate.");

        int ret; 
        if(ret = start_mapper() != 0)
            mr_err(strcat("Il processo mapper ha terminato con un errore. Errore: ",ret));
        return ret;
    }
    mr_set_mapper_pid(mr, mapper_pid);

    size_t reducer_pid = fork();
    if(reducer_pid == 0){
        // Logica reducer
        mr_log("Inizializzazione reducer");

        // Pipe
        dup2(mapper_to_reducer[0], STDIN_FILENO);
        dup2(reducer_to_main[1], STDOUT_FILENO);
        mr_log("Pipe inizializzate.");

        int ret; 
        if(ret = start_reducer() != 0)
            mr_err(strcat("Il processo reducer ha terminato con un errore. Errore: ",ret));
        return ret;
    }
    mr_set_reducer_pid(mr, reducer_pid);

    // Logica Main Process
    mr_log("Inizializzazione main process...");

    // Pipe
    dup2(reducer_to_main[0], STDIN_FILENO);
    dup2(main_to_mapper[1], STDOUT_FILENO);

    return start_main_job(mr, input_path, output_path, main_to_mapper[1], reducer_to_main[0]);
}

// Sezione "Serialize and Send": tutto ciò necessario alla scansione e l'invio dei file al mapper.
int start_main_job(mr_t mr, const char *input_path, const char *output_path, int *main_to_mapper, int *reducer_to_main){
    struct stat path_stat;
    if (stat(input_path, &path_stat) != 0) {
        mr_err("Il percorso specificato per l'input non è valido.");
        return -1;
    }

    if (S_ISREG(path_stat.st_mode)) {
        mr_log("L'input è un file singolo..");
        serialize_and_send(mr, input_path, main_to_mapper);
    } else if (S_ISDIR(path_stat.st_mode)) {
        mr_log("L'input è una directory..");
        
        filename_node* head = NULL;
        head = scan_directory(input_path, head);

        // Scorriamo la lista ordinata e inviamo i file
        filename_node* curr = head;
        while (curr != NULL) {
            serialize_and_send(mr, curr->path, main_to_mapper);

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
    close(main_to_mapper[1]);
    listen_to_reducer(reducer_to_main);
    close(reducer_to_main[0]);
    wait_for_others(mr);
    mr_destroy(mr);
    return 0;
}

// Nodo per una lista collegata di stringhe
typedef struct filename_node {
    char* path;
    struct filename_node* next;
} filename_node;

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

int serialize_and_send(mr_t mr, const char* filepath, int *main_to_mapper){
    FILE* file = fopen(filepath, "r");
    if (!file) {
        mr_err("Impossibile aprire il file per la serializzazione.");
        return -1;
    }
    
    // TODO: implementare logica di lettura riga per riga
    char* line = NULL;
    size_t len = 0;
    while (getline(&line, &len, file) != -1) {
        // Invia la riga al mapper
        write(main_to_mapper[1], line, strlen(line));
    }
    free(line);
    fclose(file);
    return 0;
}

// Sezione di ascolto dal reducer: tutto ciò che è necessario per l'ascolto dal reducer.

int listen_to_reducer(int *reducer_to_main){
    // TODO: implementare logica di ascolto del reducer e scrittura su output_path
    int token_length;
    while(readn(reducer_to_main[0], &token_length, sizeof(int)) == sizeof(int)){
        // Prendiamo il token
        // TODO: capire "limite ragionevole"
        if(token_length <= 0 || token_length > 200000){
            errno = EINVAL;
            mr_err(strcat("Lunghezza token non valida ricevuta dal Reducer: ", token_length));
            return -1;
        }
        char *token = malloc(token_length+1); // +1 per \0
        readn(reducer_to_main[0], token, token_length);
        token[token_length] = '\0';

        // Prendiamo il risultato
        int result_length;
        readn(reducer_to_main[0], &result_length, sizeof(int));
        // TODO: capire "limite ragionevole"
        if(result_length <= 0 || result_length > 200000){
            errno = EINVAL;
            mr_err(strcat("Lunghezza risultato di elaborazione del token non valida ricevuta dal Reducer: ", token_length));
            return -1;
        }

        void *result = NULL;
        if (result_length > 0) {
            result = malloc(result_length);
            readn(reducer_to_main[0], result, result_length);
        }
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
        } else if (!nread == 0) { // else EOF: la pipe è stata chiusa dall'altra parte
            left -= nread;
            ptr += nread;
        }
    }
    return (n - left); // Ritorna quanti byte è riuscito a leggere davvero
}


// Sezione dedicata ai metodi di pulizia e termine del programma
int wait_for_others(mr_t mr){
    waitpid(mr->mapper);
    waitpid(mr->reducer);

    return 0;
}

