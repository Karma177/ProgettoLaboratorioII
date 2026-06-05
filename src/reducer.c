#include "util/mr_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    struct tk_c* next;
} token_chain;

// L'item della hash contiene il puntatore al prossimo token che ha fatto collisione con lei.
// Per ogni token teniamo un altra chain (void* value) con tutti i valori legati a quel token.
typedef struct ht_i {
    const char* key;
    struct tk_c* value;
    struct ht_i* next;
    size_t counter;
    
} ht_item;

typedef struct {
    ht_item* entries;
    size_t capacity;
} ht;

typedef struct {
    mr_hash_t hash;
    int main_to_mapper;
} reader_args_t;

int destroy_chain(ht_item* entry);
size_t fallback_hash(const char* token, size_t token_len, void* user_arg);
ht_item* get_token_reference(ht* table, size_t index, const char* token);
int add_in_chain(ht* table, ht_item* token_position, token_chain* node);


// Entry point per le funzioni del reducer
int start_reducer(mr_t mr) {
    mr_log("Processo reducer avviato");

    if(mr->config.hash == NULL)
        mr_attr_set_hash_function(&mr->config, fallback_hash, NULL);

    return 0;
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
    for (size_t i = 0; i < table->capacity; i++) {
        destroy_chain(&table->entries[i]);
    }
    free(table);
    return 0;
}

int destroy_chain(ht_item* entry){
    if(entry == NULL)
        return 0;

    ht_item* t1 = entry;
    ht_item* t2 = NULL;
    do{
        t2 = t1;
        t1 = t1->next;
        free(t2);
    }while(t1 != NULL);
    
    return 0;
}

unsigned long get_hash(mr_hash_t func, void* hash_arg, const char* token, size_t token_len){
    return func(token, token_len, hash_arg);
}

int ht_insert(ht* table, unsigned long hash, const char* token, size_t token_len, void* value){
    size_t index = hash%table->capacity;

    ht_item* token_position = get_token_reference(table, index, token);
    if(token_position == NULL){
        token_position = malloc(sizeof(ht_item));
        token_position->next = NULL;
        token_position->key = token;
        token_position->value = NULL;
        token_position->counter = 0;
        
        token_position->next = table->entries;
        table->entries = token_position;
    }

    token_chain* node = malloc(sizeof(token_chain));
    node->value = value;
    node->next = NULL;

    return add_in_chain(table, token_position, node);
}

ht_item* get_token_reference(ht* table, size_t index, const char* token){
    ht_item* table_item = &table->entries[index];;
    while(table_item->next->key == token){
        table_item = table_item->next;
    };

    return table_item->next;
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
