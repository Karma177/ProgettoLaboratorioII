#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <threads.h>
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

typedef struct {
    mr_t mr;
    const char *input_path;
    const char *output_path;
} thread_args_t;

int run_mr(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    
    if (mr_start(args->mr, args->input_path, args->output_path) != 0) {
        fprintf(stderr, "Errore durante l'esecuzione del task MapReduce per %s\n", args->input_path);
        return -1;
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    const char *base_path = "examples/input_test";
    if (argc >= 2) {
        base_path = argv[1];
    }

    // Costruiamo i path interni (es. lorem.txt e test1)
    char path1[512];
    char path2[512];
    snprintf(path1, sizeof(path1), "%s/lorem.txt", base_path);
    snprintf(path2, sizeof(path2), "%s/test1", base_path);

    mr_t mr1, mr2;
    mr_attr_t attr1, attr2;

    mr_attr_init(&attr1);
    mr_attr_set_mapper_threads(&attr1, 2);
    mr_attr_set_reducer_threads(&attr1, 1);
    mr_attr_set_queue_size(&attr1, 512);
    mr_attr_set_log_file(&attr1, "word_count_lorem.log");

    mr_attr_init(&attr2);
    mr_attr_set_mapper_threads(&attr2, 2);
    mr_attr_set_reducer_threads(&attr2, 1);
    mr_attr_set_queue_size(&attr2, 512);
    mr_attr_set_log_file(&attr2, "word_count_test1.log");

    if (mr_create(&mr1, &attr1, word_count_mapper, word_count_reducer, NULL) != 0) {
        fprintf(stderr, "Errore nella creazione di mr1\n");
        return EXIT_FAILURE;
    }
    if (mr_create(&mr2, &attr2, word_count_mapper, word_count_reducer, NULL) != 0) {
        fprintf(stderr, "Errore nella creazione di mr2\n");
        return EXIT_FAILURE;
    }

    thread_args_t args1 = { mr1, path1, "test_output_lorem.txt" };
    thread_args_t args2 = { mr2, path2, "test_output_test1.txt" };

    thrd_t t1, t2;
    printf("Avvio concorrente delle due istanze di MapReduce (usando base path: %s)...\n", base_path);

    if (thrd_create(&t1, run_mr, &args1) != thrd_success) {
        fprintf(stderr, "Errore nella creazione del thread 1\n");
        return EXIT_FAILURE;
    }

    if (thrd_create(&t2, run_mr, &args2) != thrd_success) {
        fprintf(stderr, "Errore nella creazione del thread 2\n");
        return EXIT_FAILURE;
    }

    int res1 = 0, res2 = 0;
    thrd_join(t1, &res1);
    thrd_join(t2, &res2);

    mr_attr_destroy(&attr1);
    mr_attr_destroy(&attr2);

    if (res1 == 0 && res2 == 0) {
        printf("Entrambe le istanze MapReduce completate con successo!\n");
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "Si è verificato un errore in una delle due istanze MapReduce.\n");
        return EXIT_FAILURE;
    }
}
