#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mr.h>

// Funzione Mapper utente 'dummy' fornita come esempio
int dummy_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg){
    // In questo esempio, preleviamo l'intera linea di testo e lo emettiamo con token = "line"
    // Questo è il comportamento di base per un utente che implementa la libreria.
    if(line && line->line){
        emit("line", line->line, line->line_len, emit_arg);
    }
    return 0;
}

// Funzione Reducer utente 'dummy' fornita come esempio
int dummy_reducer(const char *token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg){
    // Emette un risultato fittizio uguale ad "ok", indicando completamento.
    const char *result = "ok";
    emit(token, result, strlen(result) + 1, emit_arg);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Uso: %s <input_path> <output_path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];

    mr_attr_t* attr;
    mr_t mr;

    if(attr = mr_attr_setup(10, 10, 20, NULL) == -1){
        printf("Errore: mr_attr_setup fallito.\n");
        return EXIT_FAILURE;
    }
 
    if (mr_create(&mr, attr, dummy_mapper, dummy_reducer, NULL) != 0) {
        printf("Errore: mr_create fallito. Forse parametri mancanti?\n");
        return EXIT_FAILURE;
    }

    printf("Avviando mr_start(input='%s', output='%s')...\n", input_path, output_path);
    if (mr_start(mr, input_path, output_path) != 0)
        printf("Errore: mr_start fallito.\n");
    else
        printf("Processo MapReduce concluso con SUCCESSO.\n");

    mr_attr_destroy(&attr);
    mr_destroy(mr);
    return EXIT_SUCCESS;
}
