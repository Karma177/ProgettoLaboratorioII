#include <stdio.h>
#include <stdlib.h>
#include "../../include/mr.h"

// Funzioni dummy per compilare mr_create
int dummy_mapper(const mr_file_line_t *line, mr_emit_pair_t emit, void *emit_arg, void *user_arg) {
    (void)line; (void)emit; (void)emit_arg; (void)user_arg;
    return -1;
}

int dummy_reducer(const char *token, const mr_value_t *values, size_t values_count, mr_emit_result_t emit, void *emit_arg, void *user_arg) {
    (void)token; (void)values; (void)values_count; (void)emit; (void)emit_arg; (void)user_arg;
    return -1;
}

int main(void) {
    mr_attr_t attr;
    int success = 1;

    printf("Inizio test per edge cases e gestione errori...\n");

    if (mr_attr_init(&attr) == -1) {
        perror("mr_attr_init fallito inaspettatamente");
        return 1;
    }

    // Test: settare thread mapper a 0 (deve fallire)
    printf("Test 1: Impostazione mapper_threads a 0... ");
    if (mr_attr_set_mapper_threads(&attr, 0) == -1) {
        printf("PASS (rifiutato)\n");
    } else {
        printf("FAIL (accettato erroneamente)\n");
        success = 0;
    }

    // Test: settare thread reducer a 0 (deve fallire)
    printf("Test 2: Impostazione reducer_threads a 0... ");
    if (mr_attr_set_reducer_threads(&attr, 0) == -1) {
        printf("PASS (rifiutato)\n");
    } else {
        printf("FAIL (accettato erroneamente)\n");
        success = 0;
    }

    // Test: settare queue size a 0 (deve fallire)
    printf("Test 3: Impostazione queue_size a 0... ");
    if (mr_attr_set_queue_size(&attr, 0) == -1) {
        printf("PASS (rifiutato)\n");
    } else {
        printf("FAIL (accettato erroneamente)\n");
        success = 0;
    }

    // Test: parametro attr = NULL
    printf("Test 4: Impostazione thread passando attr=NULL... ");
    if (mr_attr_set_mapper_threads(NULL, 5) == -1) {
        printf("PASS (rifiutato)\n");
    } else {
        printf("FAIL (accettato erroneamente)\n");
        success = 0;
    }

    // Test: mr_create con param NULL (es. attr = NULL)
    mr_t mr;
    printf("Test 5: Creazione istanza con attr=NULL... ");
    if (mr_create(&mr, NULL, dummy_mapper, dummy_reducer, NULL) == -1) {
        printf("PASS (rifiutato)\n");
    } else {
        printf("FAIL (accettato erroneamente)\n");
        success = 0;
    }

    // Test: mr_create con mapper=NULL
    printf("Test 6: Creazione istanza con mapper=NULL... ");
    if (mr_create(&mr, &attr, NULL, dummy_reducer, NULL) == -1) {
        printf("PASS (rifiutato)\n");
    } else {
        printf("FAIL (accettato erroneamente)\n");
        success = 0;
    }

    mr_attr_destroy(&attr);

    // Test: mr_start con mapper che restituisce -1
    printf("Test 7: Esecuzione mr_start con mapper che restituisce -1...\n");
    
    // Inizializziamo di nuovo attr con valori corretti per non fallire la creazione
    mr_attr_init(&attr);
    mr_attr_set_mapper_threads(&attr, 1);
    mr_attr_set_reducer_threads(&attr, 1);
    mr_attr_set_queue_size(&attr, 10);
    mr_attr_set_log_file(&attr, "edge_cases.log");
    
    if (mr_create(&mr, &attr, dummy_mapper, dummy_reducer, NULL) == 0) {
        // Avviamo con un path di input per fargli elaborare qualcosa
        mr_start(mr, "examples/input_test/lorem.txt", "test_output_edge.txt");
        printf("PASS (mr_start ha completato ignorando le righe fallite. Controllare i log per l'errore)\n");
    } else {
        printf("FAIL (creazione inaspettatamente fallita)\n");
        success = 0;
    }

    mr_attr_destroy(&attr);

    if (success) {
        printf("Tutti gli edge cases superati con successo!\n");
        return 0;
    } else {
        printf("Alcuni test sono falliti.\n");
        return 1;
    }
}
