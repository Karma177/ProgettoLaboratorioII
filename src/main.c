#include <stdio.h>
#include <stdlib.h>
#include <util/mr_common.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    // TODO: proper error handling
    int main_to_mapper[2];
    int mapper_to_reducer[2];
    int reducer_to_main[2];
    if(pipe(main_to_mapper) == -1 || pipe(mapper_to_reducer) == -1 || pipe(reducer_to_main) == -1)
        return -1;

    size_t mapper_pid = fork();
    if(mapper_pid == 0){
        // Logica mapper
        dup2(main_to_mapper, STDIN_FILENO);
        dup2(mapper_to_reducer, STDOUT_FILENO);

    }


    // COMUNICAZIONE SULLE PIPE
    size_t reducer_to_main = fork();
    if(reducer_to_main == 0)
        listen_to_reducer();
    size_t mapper_to_main = fork();
    if(mapper_to_main == 0)
        serialize_and_send();

    return EXIT_SUCCESS;
}



