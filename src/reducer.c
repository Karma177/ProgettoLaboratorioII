#include "util/mr_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <stdatomic.h>
#include <unistd.h>


// Numero primo relativamente grande per ottimizzare l'inserimento
// e rendere la ht abbastanza uniforme
#define HT_CAPACITY 4099

/*
    Hash table con risoluzione delle collisioni mediante chaining.
    Riadattamento di https://benhoyt.com/writings/hash-table-in-c (l'articolo non utilizza chaining)
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
    mr_hash_t hash;
    int main_to_mapper;
} reader_args_t;

typedef struct {
    ht_item** sorted_items;
    size_t item_count;
    mr_reducer_t function;
    atomic_size_t last_checked_item;
    void *user_arg;        
} worker_args_t;

ht_item** get_sorted_items(ht* table);
ht_item* get_token_reference(ht* table, size_t index, const char* token);
size_t fallback_hash(const char* token, size_t token_len, void* user_arg);
ht* ht_create();
unsigned long get_hash(mr_hash_t func, void* hash_arg, const char* token, size_t token_len);
int add_in_chain(ht* table, ht_item* token_position, token_chain* node);
int destroy_chain(ht_item* entry);
int listen_to_mapper(int fd, ht* table, mr_t mr);
int ht_insert(ht* table, unsigned long hash, const char* token, size_t token_len, void* value, size_t value_size);
int ht_destroy(ht* table);
int worker_thread_func(void* arg);
int compare_ht_items(const void* a, const void* b);
int framework_emit_result(const char *token, const void *result, size_t result_size, void *emit_arg);
int send_sorted_to_main(ht_item** sorted_items);

// Entry point per le funzioni del reducer
int start_reducer(mr_t mr) {
    mr_log("Processo reducer avviato");
    ht* hash_table = ht_create();
    if(mr->config.hash == NULL)
        mr_attr_set_hash_function(&mr->config, fallback_hash, NULL);
    listen_to_mapper(STDIN_FILENO, hash_table, mr);

    // salva tutti gli item della lista in un array e ordinali in modo lessicografico
    ht_item** sorted_items = get_sorted_items(hash_table);

    thrd_t *workers = malloc(sizeof(thrd_t) * mr->config.reducer_threads);
    int workers_created = 0;

    if (workers == NULL) {
        mr_err("Impossibile allocare l'array dei thread worker.");
    } else {
        worker_args_t w_args;
        w_args.sorted_items = sorted_items;
        w_args.function = mr->reducer_f;
        w_args.user_arg = mr->user_arg;
        atomic_init(&w_args.last_checked_item, 0);
        w_args.item_count = hash_table->counter;

        while (workers_created < mr->config.reducer_threads) {
            if (thrd_create(&workers[workers_created], worker_thread_func, &w_args) != thrd_success) {
                mr_err("Impossibile creare il thread worker.");
                break;
            }
            workers_created++;
        }

        // Aspetta che tutti i thread worker finiscano per evitare che w_args scada dallo stack
        for (int i = 0; i < workers_created; i++) {
            thrd_join(workers[i], NULL);
        }
        free(workers);
    }

    if (send_sorted_results(STDOUT_FILENO, sorted_items, hash_table->counter) < 0)
    {
        mr_err("Impossibile inviare i risultati al processo principale.");
    }

    if (sorted_items) {
        free(sorted_items);
    }

    ht_destroy(hash_table);

    return 0;
}

int worker_thread_func(void* arg){
    worker_args_t *args = (worker_args_t *)arg;
    // Worker logic
    size_t i;
    while ((i = atomic_fetch_add(&args->last_checked_item, 1)) < args->item_count) {
        // trasformazione a mr_value_t* per la funzione dell'utente
        ht_item* item = args->sorted_items[i];
        mr_value_t* values = malloc(sizeof(mr_value_t) * item->counter);
        if (values == NULL){
            mr_err("Impossibile allocare lo spazio per ritornare il messaggio in un thread worker.");
            return -1;
        }

        token_chain* curr_val = item->value;
        for (size_t id = 0; id < item->counter && curr_val != NULL; id++) {
            values[id].data = curr_val->value;
            values[id].size = curr_val->size;
            curr_val = curr_val->next;
        }

        args->function(item->key, values, item->counter, framework_emit_result, item, args->user_arg);
        free(values);
    }

    return 0;
}

int framework_emit_result(const char *token, const void *result, size_t result_size, void *emit_arg){
    ht_item *item = (ht_item *)emit_arg;
    if (item == NULL)
        return -1;

    result_chain *new_res = malloc(sizeof(result_chain));
    if (new_res == NULL){
        mr_err("Impossibile allocare memoria per il nodo del risultato.");        
        return -1;
    }

    new_res->value.size = result_size;
    new_res->value.data = NULL;
    new_res->next = NULL;

    if (result_size > 0 && result != NULL){
        void *copied_data = malloc(result_size);
        if (copied_data == NULL){
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
    }else
        item->results = new_res;

    return 0;
}

int listen_to_mapper(int fd, ht *table, mr_t mr)
{
    int token_len;
    char *token;
    int value_len;
    void *value = NULL;
    while (readn(fd, &token_len, sizeof(int)) > 0)
    {
        token = malloc(sizeof(char) * token_len + 1);
        if (readn(fd, token, token_len) < 0)
            return -1;
        token[token_len] = '\0';

        if (readn(fd, &value_len, sizeof(int)) < 0)
            return -1;

        if (value_len > 0)
        {
            value = malloc(value_len);
            if (readn(fd, value, value_len) < 0)
                return -1;
        }

        if (ht_insert(table, get_hash(mr->config.hash, mr->config.hash_arg, token, token_len), token, token_len, value, value_len) == -1)
            return -1;

        token_len = 0;
        token = NULL;
        value_len = 0;
        value = NULL;
    }

    return 0;
}

int send_sorted_to_main(ht_item** sorted_items, size_t item_count){
    if (sorted_items == NULL)
    {
        return 0;
    }

    for (size_t i = 0; i < item_count; i++)
    {
        ht_item *item = sorted_items[i];
        if (item == NULL)
        {
            continue;
        }

        // Scorriamo tutti i risultati emessi per questo specifico token
        result_chain *curr_res = item->results;
        while (curr_res != NULL)
        {
            int token_len = strlen(item->key);
            int result_len = curr_res->value.size;
            // fd è stdout, da sistemare
            // TODO Fix
            // 1. Inviamo la lunghezza del token (int)
            if (writen(fd, &token_len, sizeof(int)) < 0)
            {
                mr_err("Errore di scrittura lunghezza token sulla pipe.");
                return -1;
            }

            // 2. Inviamo il token effettivo
            if (writen(fd, item->key, token_len) < 0)
            {
                mr_err("Errore di scrittura token sulla pipe.");
                return -1;
            }

            // 3. Inviamo la lunghezza del risultato (int)
            if (writen(fd, &result_len, sizeof(int)) < 0)
            {
                mr_err("Errore di scrittura lunghezza risultato sulla pipe.");
                return -1;
            }

            // 4. Inviamo i byte del risultato (se presenti)
            if (result_len > 0 && curr_res->value.data != NULL)
            {
                if (writen(fd, curr_res->value.data, result_len) < 0)
                {
                    mr_err("Errore di scrittura dati risultato sulla pipe.");
                    return -1;
                }
            }

            curr_res = curr_res->next;
        }
    }
    return 0;
}

// Funzione helper per ordinare gli elementi
int compare_ht_items(const void* a, const void* b) {
    ht_item* itemA = *(ht_item**)a;
    ht_item* itemB = *(ht_item**)b;
    return strcmp(itemA->key, itemB->key);
}

// Funzione helper per ottenere l'array ordinato
ht_item** get_sorted_items(ht* table) {
    if (table->counter == 0) return NULL;
    
    ht_item** sorted_items = malloc(table->counter * sizeof(ht_item*));
    if (!sorted_items) {
        mr_err("Impossibile allocare l'array per l'ordinamento");
        return NULL;
    }
    
    size_t index = 0;
    for (size_t i = 0; i < table->capacity; i++) {
        ht_item* curr = table->entries[i].next;
        while (curr != NULL) {
            sorted_items[index++] = curr;
            curr = curr->next;
        }
    }
    
    qsort(sorted_items, table->counter, sizeof(ht_item*), compare_ht_items);
    return sorted_items;
}



// Struttura dati della hash table

// source: https://benhoyt.com/writings/hash-table-in-c
ht* ht_create(){
    ht* table = malloc(sizeof(ht));
    if(table == NULL){
        mr_err("Impossibile creare l'hash table.");
        return NULL;
    }

    table->capacity = HT_CAPACITY;
    table->counter = 0;
    // calloc per inizializzare la memoria che allochiamo a 0    
    table->entries = calloc(table->capacity, sizeof(ht_item));
    if(table->entries == NULL){
        mr_err("Impossibile allocare lo spazio per gli item della hash table.");
        free(table);
        return NULL;
    }

    return table;
}

int ht_destroy(ht* table){
    if (table == NULL)
        return 0;
    for (size_t i = 0; i < table->capacity; i++) {
        destroy_chain(table->entries[i].next);
    }
    free(table->entries);
    free(table);
    return 0;
}

int destroy_chain(ht_item* entry){
    ht_item* curr = entry;
    while(curr != NULL){
        ht_item* next_item = curr->next;

        // Libera la catena di valori (token_chain) e relativi payload
        token_chain* curr_val = curr->value;
        while(curr_val != NULL){
            token_chain* next_val = curr_val->next;
            if(curr_val->value != NULL)
                free(curr_val->value);
            free(curr_val);
            curr_val = next_val;
        }

        // Libera il token associato
        if(curr->key != NULL)
            free((void*)curr->key);

        free(curr);
        curr = next_item;
    }
    return 0;
}

unsigned long get_hash(mr_hash_t func, void* hash_arg, const char* token, size_t token_len){
    return func(token, token_len, hash_arg);
}

int ht_insert(ht* table, unsigned long hash, const char* token, size_t token_len, void* value, size_t value_size){
    size_t index = hash%table->capacity;

    ht_item* token_position = get_token_reference(table, index, token);
    if(token_position == NULL){
        token_position = malloc(sizeof(ht_item));
        if(token_position == NULL){
            mr_err("Impossibile allocare spazio per inserire un item nella table.");
            free((void*)token); 
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
        return -1;
    }
    node->value = value;
    node->size = value_size;
    node->next = NULL;

    return add_in_chain(table, token_position, node);
}

ht_item* get_token_reference(ht* table, size_t index, const char* token){
    ht_item* curr = table->entries[index].next;
    while(curr != NULL){
        if(curr->key != NULL && strcmp(curr->key, token) == 0)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

int add_in_chain(ht* table, ht_item* token_position, token_chain* node){
    if(table == NULL || token_position == NULL || node == NULL){
        mr_err("Errore nei parametri durante l'inserimento di un valore in coda a una chain.");
        return -1;
    }

    token_chain* temp = token_position->value;
    if(temp != NULL){
        node->next = token_position->value;
        token_position->value = node;
    }else
        token_position->value = node;
    token_position->counter++;

    #if DEBUG
        char print[256];
        snprintf(print, sizeof(print), "Token inserito nella hash table. Token: %s, Count: %zu", token_position->key, token_position->counter);
        mr_debug(print);
    #endif

    return 0;
}

mr_value_t* get_values_from_token(ht* table, unsigned long hash, const char* token){
    size_t index = hash % table->capacity;
    ht_item* item = &table->entries[index];
    while(item != NULL){
        if(item->key != NULL && strcmp(item->key, token) == 0) {
            break;
        }
        item = item->next;
    }
    if(item == NULL){
        char message[256];
        snprintf(message, sizeof(message),"Non esiste alcun valore per il token: %s",token);
        mr_err(message);
        return NULL;
    }

    mr_value_t* array = malloc(sizeof(mr_value_t) * item->counter);
    if(array == NULL){ 
        mr_err("Impossibile allocare un array per ritornare i valori.");
        return NULL;
    }

    token_chain* temp = item->value;
    int i = 0;
    while(temp != NULL){
        array[i].data = temp->value;
        array[i].size = temp->size;
        i++;
        temp = temp->next;
    }
    
    return array;
}


// utils
size_t fallback_hash(const char* token, size_t token_len, void* user_arg){
    size_t hash = 5381;
    for (size_t i = 0; i < token_len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)token[i];
    }
    return hash;
}
