#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../include/mr.h"

// Mapper: per ogni parola trovata, emette (parola, "nome_file:numero_riga")
int inverted_index_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg) {
    if (!line || !line->line || line->line_len == 0) return 0;
    
    // copia intera linea manipolabile
    char *buffer = malloc(line->line_len + 1);
    if (!buffer) return -1;
    memcpy(buffer, line->line, line->line_len);
    buffer[line->line_len] = '\0';
    
    for (int i = 0; buffer[i]; i++)
        buffer[i] = tolower((unsigned char)buffer[i]);

    // copia nome file
    char value_str[256];
    char file_name_buf[256];
    size_t fn_len = line->file_name_len < 255 ? line->file_name_len : 255;
    if (line->file_name){
        memcpy(file_name_buf, line->file_name, fn_len);
    }else{
        fn_len = 0;
    }
    file_name_buf[fn_len] = '\0';
    
    // trova nome file senza il resto del path
    char *base_name = strrchr(file_name_buf, '/');
    if (base_name) base_name++;
    else base_name = file_name_buf;

    // costruisce la stringa associata al token da stampare
    int val_len = snprintf(value_str, sizeof(value_str), "%s:%lu", base_name, line->line_number);

    char *saveptr;
    char *word = strtok_r(buffer, " \t\n\r.,;:!?()[]{}\"'", &saveptr);
    while (word != NULL) {
        emit(word, value_str, val_len, emit_arg);
        word = strtok_r(NULL, " \t\n\r.,;:!?()[]{}\"'", &saveptr);
    }
    
    free(buffer);
    return 0;
}

// Reducer: riceve la parola e la lista dei valori associati (es. "file1.txt:10", "file2.txt:4")
// Invece di sommarli, emette ciascun valore come risultato distinto associato alla parola.
int inverted_index_reducer(const char *token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg) {
    (void)user_arg;
    
    // Emette un risultato separato per ogni occorrenza/valore della parola
    for (size_t i = 0; i < values_count; i++) {
        emit(token, values[i].data, values[i].size, emit_arg);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    const char *input_path = "examples/input_test";
    const char *output_path = "test_output_multi_result.txt";

    if (argc >= 3) {
        input_path = argv[1];
        output_path = argv[2];
    } else if (argc == 2) {
        input_path = argv[1];
    }

    mr_attr_t attr;
    mr_attr_init(&attr);
    mr_attr_set_mapper_threads(&attr, 2);
    mr_attr_set_reducer_threads(&attr, 2);
    mr_attr_set_queue_size(&attr, 1024);
    mr_attr_set_log_file(&attr, "inverted_index.log");
    
    mr_t mr;
    if (mr_create(&mr, &attr, inverted_index_mapper, inverted_index_reducer, NULL) != 0) {
        fprintf(stderr, "Errore nella creazione dell'istanza MapReduce\n");
        return EXIT_FAILURE;
    }

    printf("Avvio inverted index da '%s' a '%s'...\n", input_path, output_path);
    if (mr_start(mr, input_path, output_path) != 0) {
        fprintf(stderr, "Errore durante l'esecuzione del task MapReduce\n");
        return EXIT_FAILURE;
    }
    
    printf("Completato con successo.\n");
    mr_attr_destroy(&attr);
    return EXIT_SUCCESS;
}
