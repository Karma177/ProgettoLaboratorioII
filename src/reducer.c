#include "util/mr_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>


// Numero primo relativamente grande per ottimizzare l'inserimento
// e rendere la ht abbastanza uniforme
#define HT_CAPACITY 4099

// --- STRUTTURE DATI ----

/*
    Hash table con risoluzione delle collisioni mediante chaining.
    Inserimento in testa di ogni entry.
    -   token_chain: nodo contenente effettivamente il valore legato al token
    -   ht_item: nodo necessario per risolvere le collisioni di token,
        contiene token, i valori legati ad esso, e il prossimo nodo ht_item.
    -   ht: la hash table effettiva 
*/
typedef struct tk_c{
    void* value;
    size_t size;
    struct tk_c* next;
} token_chain;

typedef struct res_c {
    mr_value_t value;    // Contiene .data (puntatore alla copia sull'heap) e .size (dimensione)
    struct res_c* next;
} result_chain;

// L'item della hash contiene il puntatore al prossimo token che ha fatto collisione con lei.
// Per ogni token teniamo un altra chain (void* value) con tutti i valori legati a quel token.
typedef struct ht_i {
    const char* key;
    struct tk_c* value;
    struct ht_i* next;
    result_chain* results;
    size_t counter;
} ht_item;

typedef struct {
    ht_item* entries;
    size_t counter;
    size_t capacity;
} ht;

typedef struct {
    ht_item** items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    mtx_t mutex;
    cnd_t not_full;
    cnd_t not_empty;
    int closed; // Flag per segnalare che il produttore ha finito
} ht_queue_t;

typedef struct {
    ht_queue_t *queue;
    mr_reducer_t function;
    void *user_arg;        
    mr_t mr;
} worker_args_t;

// --- PROTOTIPI ----

// Funzioni principali
int start_reducer(mr_t mr);
int listen_to_mapper(ht* table, mr_t mr);

// Metodi Hash Table
ht* ht_create();
int ht_destroy(ht* table);
int ht_insert(ht* table, unsigned long hash, const char* token, size_t token_len, void* value, size_t value_size);
unsigned long get_hash(mr_hash_t func, void* hash_arg, const char* token, size_t token_len);
ht_item* get_token_reference(ht* table, size_t index, const char* token);
int add_in_chain(ht* table, ht_item* token_position, token_chain* node);
int destroy_chain(ht_item* entry);

// Metodi Produttore-Consumatore
int ht_queue_init(ht_queue_t *q, size_t capacity);
void ht_queue_destroy(ht_queue_t *q);
void ht_queue_close(ht_queue_t *q);
int ht_queue_push(ht_queue_t *q, ht_item *item);
int ht_queue_pop(ht_queue_t *q, ht_item **item);
int reducer_worker_thread_func(void* arg);
int framework_emit_result(const char *token, const void *result, size_t result_size, void *emit_arg);

// Metodi Ausiliari
ht_item** get_sorted_items(ht* table);
int compare_ht_items(const void* a, const void* b);
int send_sorted_results(ht_item** sorted_items, size_t item_count);
size_t fallback_hash(const char* token, size_t token_len, void* user_arg);


// --- FUNZIONI PRINCIPALI ----

int start_reducer(mr_t mr) {
    char log_msg[128];
    mr_log("Processo reducer avviato");
    
    ht* hash_table = ht_create();
    if(hash_table == NULL){
        mr_err("Impossibile creare la hash table per il reducer.");
        return -1;
    }
    
    if(mr->config.hash == NULL)
        mr_attr_set_hash_function(&mr->config, fallback_hash, NULL);
        
    if(listen_to_mapper(hash_table, mr) < 0){
        mr_err("Errore durante l'ascolto dal mapper.");
        ht_destroy(hash_table);
        return -1;
    }

    snprintf(log_msg, sizeof(log_msg), "Raggruppati %zu token distinti", hash_table->counter);
    mr_log_event("METRIC_TOKENS", log_msg);

    // Salva tutti gli item della lista in un array e ordinali in modo lessicografico
    ht_item** sorted_items = get_sorted_items(hash_table);
    if(hash_table->counter > 0 && sorted_items == NULL){
        mr_err("Impossibile ordinare gli elementi della hash table.");
        ht_destroy(hash_table);
        return -1;
    }

    int status = 0;
    if(hash_table->counter > 0){
        thrd_t *workers = malloc(sizeof(thrd_t) * mr->config.reducer_threads);
        if(workers == NULL){
            mr_err("Impossibile allocare l'array dei thread worker.");
            free(sorted_items);
            ht_destroy(hash_table);
            return -1;
        }

        ht_queue_t queue;
        if(ht_queue_init(&queue, mr->config.queue_size) < 0){
            mr_err("Impossibile inizializzare la coda del reducer.");
            free(workers);
            free(sorted_items);
            ht_destroy(hash_table);
            return -1;
        }

        worker_args_t w_args;
        w_args.queue = &queue;
        w_args.function = mr->reducer_f;
        w_args.user_arg = mr->user_arg;
        w_args.mr = mr;

        size_t workers_created = 0;
        while(workers_created < mr->config.reducer_threads){
            if(thrd_create(&workers[workers_created], reducer_worker_thread_func, &w_args) != thrd_success){
                mr_err("Impossibile creare il thread worker.");
                status = -1;
                break;
            }
            workers_created++;
        }

        // Se non ci sono stati errori nella creazione, avvia il job
        if(status == 0){
            // Produttore: pusha tutti i token nella coda
            for(size_t i = 0; i < hash_table->counter; i++)
                ht_queue_push(&queue, sorted_items[i]);
        }
        
        // Chiudi la coda per segnalare ai worker che non ci sono più elementi
        ht_queue_close(&queue);

        // Aspetta che tutti i thread worker finiscano
        for(size_t i = 0; i < workers_created; i++)
            thrd_join(workers[i], NULL);
            
        free(workers);
        ht_queue_destroy(&queue);
    }

    if(status == 0)
        if(send_sorted_results(sorted_items, hash_table->counter) < 0){
            mr_err("Impossibile inviare i risultati al processo principale.");
            status = -1;
        }

    if(sorted_items)
        free(sorted_items);
    ht_destroy(hash_table);

    return status;
}

int listen_to_mapper(ht *table, mr_t mr) {
    int token_len;
    char *token;
    int value_len;
    void *value = NULL;
    
    while(readn(STDIN_FILENO, &token_len, sizeof(int)) > 0){
        if(token_len <= 0 || token_len > LIMITE_RAGIONEVOLE){
            mr_err("Lunghezza token non valida ricevuta dal mapper.");
            return -1;
        }

        token = malloc(sizeof(char) * token_len + 1);
        if(token == NULL){
            mr_err("Impossibile allocare memoria per il token.");
            return -1;
        }

        if(readn(STDIN_FILENO, token, token_len) < 0){
            free(token);
            return -1;
        }
        token[token_len] = '\0';

        if(readn(STDIN_FILENO, &value_len, sizeof(int)) < 0){
            free(token);
            return -1;
        }

        if(value_len < 0 || value_len > LIMITE_RAGIONEVOLE){
            mr_err("Lunghezza valore non valida ricevuta dal mapper.");
            free(token);
            return -1;
        }

        if(value_len > 0){
            value = malloc(value_len);
            if(value == NULL){
                mr_err("Impossibile allocare memoria per il valore.");
                free(token);
                return -1;
            }
            if(readn(STDIN_FILENO, value, value_len) < 0){
                free(token);
                free(value);
                return -1;
            }
        }

        if(ht_insert(table, get_hash(mr->config.hash, mr->config.hash_arg, token, token_len), token, token_len, value, value_len) == -1)
            return -1;

        token_len = 0;
        token = NULL;
        value_len = 0;
        value = NULL;
    }

    return 0;
}


// --- METODI PRODUTTORE-CONSUMATORE ----

int ht_queue_init(ht_queue_t *q, size_t capacity) {
    q->items = malloc(capacity * sizeof(ht_item*));
    if(!q->items) return -1;
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->closed = 0;
    
    if(mtx_init(&q->mutex, mtx_plain) != thrd_success){
        free(q->items);
        return -1;
    }
    if(cnd_init(&q->not_full) != thrd_success){
        mtx_destroy(&q->mutex);
        free(q->items);
        return -1;
    }
    if(cnd_init(&q->not_empty) != thrd_success){
        cnd_destroy(&q->not_full);
        mtx_destroy(&q->mutex);
        free(q->items);
        return -1;
    }
    return 0;
}

void ht_queue_destroy(ht_queue_t *q) {
    if(q->items) free(q->items);
    mtx_destroy(&q->mutex);
    cnd_destroy(&q->not_full);
    cnd_destroy(&q->not_empty);
}

void ht_queue_close(ht_queue_t *q) {
    mtx_lock(&q->mutex);
    q->closed = 1;
    // Sveglia tutti i consumer in attesa
    cnd_broadcast(&q->not_empty);
    mtx_unlock(&q->mutex);
}

int ht_queue_push(ht_queue_t *q, ht_item *item) {
    mtx_lock(&q->mutex);
    while(q->count == q->capacity)
        cnd_wait(&q->not_full, &q->mutex);
        
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    // Segnala nuovo elemento
    cnd_signal(&q->not_empty);
    mtx_unlock(&q->mutex);
    return 0;
}

int ht_queue_pop(ht_queue_t *q, ht_item **item) {
    mtx_lock(&q->mutex);
    while(q->count == 0 && !q->closed)
        cnd_wait(&q->not_empty, &q->mutex);
    
    if(q->count == 0 && q->closed){
        mtx_unlock(&q->mutex);
        return -1; 
    }
    
    *item = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    // Segnala spazio libero
    cnd_signal(&q->not_full);
    mtx_unlock(&q->mutex);
    return 0;
}

int reducer_worker_thread_func(void* arg) {
    worker_args_t *args = (worker_args_t *)arg;
    set_log_mr(args->mr);
    ht_item *item;
    mr_log_event("THREAD_START", "Avviato thread reducer worker");

    while(ht_queue_pop(args->queue, &item) == 0){
        mr_value_t* values = malloc(sizeof(mr_value_t) * item->counter);
        if(values == NULL){
            mr_err("Impossibile allocare lo spazio per ritornare il messaggio in un thread worker.");
            return -1;
        }

        token_chain* curr_val = item->value;
        for(size_t id = 0; id < item->counter && curr_val != NULL; id++){
            values[id].data = curr_val->value;
            values[id].size = curr_val->size;
            curr_val = curr_val->next;
        }

        if (args->function(item->key, values, item->counter, framework_emit_result, item, args->user_arg) != 0) {
            mr_err("Il reducer fornito dall'utente ha restituito un errore (-1) per un token.");
        }
        free(values);
    }
    mr_log_event("THREAD_END", "Terminato thread reducer worker");
    return 0;
}

int framework_emit_result(const char *token, const void *result, size_t result_size, void *emit_arg) {
    (void)token;
    ht_item *item = (ht_item *)emit_arg;
    if(item == NULL) return -1;

    result_chain *new_res = malloc(sizeof(result_chain));
    if(new_res == NULL){
        mr_err("Impossibile allocare memoria per il nodo del risultato.");        
        return -1;
    }

    new_res->value.size = result_size;
    new_res->value.data = NULL;
    new_res->next = NULL;

    if(result_size > 0 && result != NULL){
        void *copied_data = malloc(result_size);
        if(copied_data == NULL){
            free(new_res);
            mr_err("Impossibile allocare memoria per i dati del risultato.");         
            return -1;
        }
        memcpy(copied_data, result, result_size);
        new_res->value.data = copied_data;
    }

    if(item->results != NULL){
        new_res->next = item->results;
        item->results = new_res;
    } else {
        item->results = new_res;
    }

    return 0;
}


// --- METODI HASH TABLE ----

ht* ht_create() {
    ht* table = malloc(sizeof(ht));
    if(table == NULL){
        mr_err("Impossibile creare l'hash table.");
        return NULL;
    }

    table->capacity = HT_CAPACITY;
    table->counter = 0;
    table->entries = calloc(table->capacity, sizeof(ht_item));
    if(table->entries == NULL){
        mr_err("Impossibile allocare lo spazio per gli item della hash table.");
        free(table);
        return NULL;
    }

    return table;
}

int ht_destroy(ht* table) {
    if(table == NULL) return 0;
    
    for(size_t i = 0; i < table->capacity; i++)
        destroy_chain(table->entries[i].next);
        
    free(table->entries);
    free(table);
    return 0;
}

unsigned long get_hash(mr_hash_t func, void* hash_arg, const char* token, size_t token_len) {
    return func(token, token_len, hash_arg);
}

int ht_insert(ht* table, unsigned long hash, const char* token, size_t token_len, void* value, size_t value_size) {
    (void)token_len;
    size_t index = hash % table->capacity;

    ht_item* token_position = get_token_reference(table, index, token);
    if(token_position == NULL){
        token_position = malloc(sizeof(ht_item));
        if(token_position == NULL){
            mr_err("Impossibile allocare spazio per inserire un item nella table.");
            free((void*)token); 
            if(value) free(value);
            return -1;
        }
        token_position->next = NULL;
        token_position->key = token;
        token_position->value = NULL;
        token_position->counter = 0;
        token_position->results = NULL;
        
        token_position->next = table->entries[index].next;
        table->entries[index].next = token_position;
        table->counter++;
    } else {
        free((void*)token);
    }

    token_chain* node = malloc(sizeof(token_chain));
    if(node == NULL){
        mr_err("Impossibile allocare spazio per inserire un item nella chain dei token.");
        if(value) free(value);
        return -1;
    }
    node->value = value;
    node->size = value_size;
    node->next = NULL;

    return add_in_chain(table, token_position, node);
}

ht_item* get_token_reference(ht* table, size_t index, const char* token) {
    ht_item* curr = table->entries[index].next;
    while(curr != NULL){
        if(curr->key != NULL && strcmp(curr->key, token) == 0)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

int add_in_chain(ht* table, ht_item* token_position, token_chain* node) {
    if(table == NULL || token_position == NULL || node == NULL){
        mr_err("Errore nei parametri durante l'inserimento di un valore in coda a una chain.");
        return -1;
    }

    token_chain* temp = token_position->value;
    if(temp != NULL){
        node->next = token_position->value;
        token_position->value = node;
    } else {
        token_position->value = node;
    }
    token_position->counter++;

    #if DEBUG
    char print[256];
    snprintf(print, sizeof(print), "Token inserito nella hash table. Token: %s, Count: %zu", token_position->key, token_position->counter);
    mr_debug(print);
    #endif

    return 0;
}

int destroy_chain(ht_item* entry) {
    ht_item* curr = entry;
    while(curr != NULL){
        ht_item* next_item = curr->next;

        token_chain* curr_val = curr->value;
        while(curr_val != NULL){
            token_chain* next_val = curr_val->next;
            if(curr_val->value != NULL)
                free(curr_val->value);
            free(curr_val);
            curr_val = next_val;
        }

        result_chain* curr_res = curr->results;
        while(curr_res != NULL){
            result_chain* next_res = curr_res->next;
            if(curr_res->value.data != NULL)
                free((void*)curr_res->value.data);
            free(curr_res);
            curr_res = next_res;
        }

        if(curr->key != NULL)
            free((void*)curr->key);

        free(curr);
        curr = next_item;
    }
    return 0;
}


// --- METODI AUSILIARI ----

size_t fallback_hash(const char* token, size_t token_len, void* user_arg) {
    (void)user_arg;
    size_t hash = 5381;
    for(size_t i = 0; i < token_len; i++)
        hash = ((hash << 5) + hash) + (unsigned char)token[i];
    return hash;
}

int compare_ht_items(const void* a, const void* b) {
    ht_item* itemA = *(ht_item**)a;
    ht_item* itemB = *(ht_item**)b;
    return strcmp(itemA->key, itemB->key);
}

ht_item** get_sorted_items(ht* table) {
    if(table->counter == 0) return NULL;
    
    ht_item** sorted_items = malloc(table->counter * sizeof(ht_item*));
    if(!sorted_items){
        mr_err("Impossibile allocare l'array per l'ordinamento");
        return NULL;
    }
    
    size_t index = 0;
    for(size_t i = 0; i < table->capacity; i++){
        ht_item* curr = table->entries[i].next;
        while(curr != NULL){
            sorted_items[index++] = curr;
            curr = curr->next;
        }
    }
    
    qsort(sorted_items, table->counter, sizeof(ht_item*), compare_ht_items);
    return sorted_items;
}

int send_sorted_results(ht_item** sorted_items, size_t item_count) {
    char log_msg[128];
    if(sorted_items == NULL) return 0;

    size_t results_count = 0;
    for(size_t i = 0; i < item_count; i++){
        ht_item *item = sorted_items[i];

        // Conta il numero di risultati per questo token
        int num_results = 0;
        result_chain *curr = item->results;
        while (curr != NULL) {
            num_results++;
            curr = curr->next;
        }

        int token_len = strlen(item->key);
        if(writen(STDOUT_FILENO, &token_len, sizeof(int)) < 0) return -1;
        if(writen(STDOUT_FILENO, item->key, token_len) < 0) return -1;
        if(writen(STDOUT_FILENO, &num_results, sizeof(int)) < 0) return -1;

        result_chain *curr_res = item->results;
        while(curr_res != NULL){
            results_count++;
            int result_len = curr_res->value.size;
            
            if (writen(STDOUT_FILENO, &result_len, sizeof(int)) < 0) return -1;
            if(result_len > 0 && curr_res->value.data != NULL){
                if(writen(STDOUT_FILENO, curr_res->value.data, result_len) < 0) return -1;
            }
            curr_res = curr_res->next;
        }
    }

    snprintf(log_msg, sizeof(log_msg), "Generati %zu risultati finali", results_count);
    mr_log_event("METRIC_RESULTS", log_msg);

    return 0;
}
