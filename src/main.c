#include <stdio.h>
#include <stdlib.h>
#include <util/mr_common.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        mr_err("Argomenti insufficienti. Utilizzo ottimale: eseguibile <input_path> <output_path>");
        return EXIT_FAILURE;
    }

    mr_log("Avvio processo map/reduce tramite mr_start...");
    if (mr_start(argv[1], argv[2]) != 0) {
        mr_err("Esecuzione di mr_start fallita.");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int mr_start(const char *input_path, const char *output_path){
    if (input_path == NULL || output_path == NULL) {
        return -1;
        mr_err("Utilizzo 'mr_start' errato. Uscendo...");
    }

    // TODO: proper error handling
    int main_to_mapper[2];
    int mapper_to_reducer[2];
    int reducer_to_main[2];
    mr_t mapreducer = malloc(sizeof(struct mr));
    set_log_mr(mapreducer);
    mapreducer->config = mr_attr_setup(10, 10, 20, NULL);
    mr_set_main_pid(mapreducer, getpid());
    
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

        start_mapper();
        return 1;
    }
    mr_set_mapper_pid(mapreducer, mapper_pid);

    size_t reducer_pid = fork();
    if(reducer_pid == 0){
        // Logica reducer
        mr_log("Inizializzazione reducer");

        // Pipe
        dup2(mapper_to_reducer[0], STDIN_FILENO);
        dup2(reducer_to_main[1], STDOUT_FILENO);
        mr_log("Pipe inizializzate.");

        start_reducer();
        return 1;
    }
    mr_set_reducer_pid(mapreducer, reducer_pid);

    // Logica Main Process
    mr_log("Inizializzazione main process...");

    // Pipe
    dup2(reducer_to_main[0], STDIN_FILENO);
    dup2(main_to_mapper[1], STDOUT_FILENO);

    start_main_job(mapreducer, input_path, output_path, main_to_mapper[1], reducer_to_main[0]);

    return 0;
}

int start_main_job(mr_t mr, const char *input_path, const char *output_path, int main_to_mapper, int reducer_to_main){
    struct stat path_stat;
    if (stat(input_path, &path_stat) != 0) {
        mr_err("Il percorso specificato per l'input non è valido.");
        return -1;
    }

    // Divisione in thread?
    // Il lavoro di serializzazione potrebbe continuare anche quando il reducer ha già terminato una parte del suo lavoro.
    // Di sicuro la soluzione più ottimale sarebbe quella di dividere in due thread; listener e sender.
    // TODO: Divisione in thread del main process. Listener e sender, scegliere inoltre un orchestrator per aspettare il termine di tutte le fork.

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

int serialize_and_send(mr_t mr, const char* filepath, int main_to_mapper){
    FILE* file = fopen(filepath, "r");
    if (!file) {
        mr_err("Impossibile aprire il file per la serializzazione.");
        return -1;
    }
    
    // TODO: implementare logica di lettura riga per riga

    fclose(file);
    return 0;
}



