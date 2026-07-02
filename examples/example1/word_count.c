#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../include/mr.h"

// Funzione Mapper: estrae parole dalla riga e per ognuna emette (parola, NULL)
int word_count_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg) {
    (void)user_arg;
    if (!line || !line->line || line->line_len == 0) return 0;
    
    char *buffer = strdup(line->line);
    if (!buffer) return -1;
    
    for (int i = 0; buffer[i]; i++)
        buffer[i] = tolower((unsigned char)buffer[i]);

    char *saveptr;
    char *word = strtok_r(buffer, " \t\n\r.,;:!?()[]{}\"'", &saveptr);
    while (word != NULL) {
        emit(word, NULL, 0, emit_arg);
        word = strtok_r(NULL, " \t\n\r.,;:!?()[]{}\"'", &saveptr);
    }
    
    free(buffer);
    return 0;
}

// Funzione Reducer: riceve la parola e la lista di valori (i NULL). La dimensione della lista è il conteggio.
int word_count_reducer(const char *token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg) {
    (void)values;
    (void)user_arg;
    char result_str[64];
    int len = snprintf(result_str, sizeof(result_str), "%zu", values_count);
    if (len > 0)
        emit(token, result_str, len, emit_arg);

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <input_path> <output_path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];

    mr_attr_t attr;
    mr_attr_init(&attr);
    mr_attr_set_mapper_threads(&attr, 4);
    mr_attr_set_reducer_threads(&attr, 2);
    mr_attr_set_queue_size(&attr, 1024);
    mr_attr_set_log_file(&attr, "word_count.log");
    
    mr_t mr;
    if (mr_create(&mr, &attr, word_count_mapper, word_count_reducer, NULL) != 0) {
        fprintf(stderr, "Errore nella creazione dell'istanza MapReduce\n");
        return EXIT_FAILURE;
    }

    printf("Avvio word count da '%s' a '%s'...\n", input_path, output_path);
    if (mr_start(mr, input_path, output_path) != 0) {
        fprintf(stderr, "Errore durante l'esecuzione del task MapReduce\n");
        return EXIT_FAILURE;
    }
    
    printf("Completato con successo.\n");
    mr_attr_destroy(&attr);
    return EXIT_SUCCESS;
}
