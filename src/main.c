#include <stdio.h>
#include <stdlib.h>
#include <util/mr_common.h>
#include <unistd.h>
#include "mr_common.h"

// Variabile globale per il logging all'interno di questo file
static mr_t global_mr = NULL;

int main(int argc, char *argv[]) {
    // TODO: proper error handling
    int main_to_mapper[2];
    int mapper_to_reducer[2];
    int reducer_to_main[2];
    mr_t mapreducer = malloc(sizeof(struct mr));
    
    // Inizializza la variabile globale affinché log() possa usarla
    global_mr = mapreducer;
    mr_set_main_pid(mapreducer, getpid());
    

    if(pipe(main_to_mapper) == -1 || pipe(mapper_to_reducer) == -1 || pipe(reducer_to_main) == -1)
        return -1;
    
    dup2(reducer_to_main[0], STDIN_FILENO);
    dup2(main_to_mapper[1], STDOUT_FILENO);

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
    

    return EXIT_SUCCESS;
}

void mr_log(const char* message){
    if (global_mr == NULL) return; // Sicurezza nel caso log() venga chiamata prima di inizializzare mapreducer
    
    char* process = "UNKNOWN";

    if(getpid() == global_mr->mapper)
        process = "MAPPER";
    if(getpid() == global_mr->reducer)
        process = "REDUCER";
    if(getpid() == global_mr->main)
        process = "MAIN";

    write_to_log(global_mr->config.log_file, process, message);
}



