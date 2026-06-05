#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include "mr_common.h"



// TODO: reindirizzare errno a logger, e dove vi è "return -1" o NULL scrivere la causa nei log. 

// AGGIUNTA RISPETTO AL TESTO: per semplificare operazioni di debug
// Utilizza i metodi che sarebbero stati utilizzati dall'utente che usa la nostra libreria, in un unica volta.
// Gli altri metodi rimangono comunque a dispozione dell'utente in caso volesse cambiare qualcosa.
// In caso vengano forniti parametri nulli utilizza come fallback quelli definiti in mr_attr_init.
mr_attr_t* mr_attr_setup(size_t mapthreads, size_t reducerthreads, size_t queuesize, char* logfile){
    mr_attr_t* attributes = malloc(sizeof(mr_attr_t));
    if (attributes == NULL) {
        mr_err("mr_attr_setup: memoria insufficiente (malloc fallita).");
        return NULL;
    }
    
    // Inizializza con i valori di default
    if (mr_attr_init(attributes) == -1) {
        mr_err("mr_attr_setup: fallita l'inizializzazione degli attributi (mr_attr_init).");
        free(attributes);
        return NULL;
    }
    if(mapthreads > 0 && mapthreads < 200)
        mr_attr_set_mapper_threads(attributes, mapthreads);
    if(reducerthreads > 0 && reducerthreads < 200)
        mr_attr_set_reducer_threads(attributes, reducerthreads);
    if(queuesize > 0 && queuesize < 200)
        mr_attr_set_queue_size(attributes, queuesize);
    
    if(logfile == NULL) {
        logfile = generate_log_header();
    }
    mr_attr_set_log_file(attributes, logfile);
    // TODO: print attributes su logs (e su console)
    
    return attributes;
}

int mr_create(mr_t *mr, const mr_attr_t *attr, mr_mapper_t mapper, mr_reducer_t reducer, void *user_arg){
    if(mr == NULL || attr == NULL || mapper == NULL || reducer == NULL) {
        mr_err("mr_create: parametri invalidi (argomenti NULL).");
        return -1;
    }
    
    mr_t temp = malloc(sizeof(struct mr));
    if(temp == NULL) {
        mr_err("mr_create: memoria insufficiente (malloc fallita).");
        return -1;
    }
    
    temp->config = *attr;
    temp->mapper_f = mapper;
    temp->reducer_f = reducer;
    temp->user_arg = user_arg;
    
    *mr = temp;
    
    #if DEBUG
    set_log_mr(temp);
    mr_debug("mr_create: Struttura MapReduce creata con successo.");
    #endif

    return 0;
}

int mr_attr_init(mr_attr_t *attr){
    if (attr == NULL) {
        mr_err("mr_attr_init: parametro 'attr' è NULL.");
        return -1;
    }

    attr->mapper_threads = 3;
    attr->reducer_threads = 3;
    attr->queue_size = 10;
    attr->log_file = NULL;
    attr->hash = NULL;
    attr->hash_arg = NULL;
    
    return 0;
}

int mr_attr_destroy(mr_attr_t *attr) {
    if (attr == NULL) {
        mr_err("mr_attr_destroy: parametro 'attr' è NULL.");
        return -1;
    }

    if (attr->log_file != NULL)
        free((void *)attr->log_file); 
    
    free(attr);
    return 0;
}

int mr_attr_set_mapper_threads(mr_attr_t *attr, size_t n) {
    if (attr == NULL) {
        mr_err("mr_attr_set_mapper_threads: parametro 'attr' è NULL.");
        return -1;
    }
    attr->mapper_threads = n;
    return 0;
}

int mr_attr_set_reducer_threads(mr_attr_t *attr, size_t n) {
    if (attr == NULL) {
        mr_err("mr_attr_set_reducer_threads: parametro 'attr' è NULL.");
        return -1;
    }
    attr->reducer_threads = n;
    return 0;
}

int mr_attr_set_queue_size(mr_attr_t *attr, size_t n) {
    if (attr == NULL) {
        mr_err("mr_attr_set_queue_size: parametro 'attr' è NULL.");
        return -1;
    }
    attr->queue_size = n;
    return 0;
}

int mr_attr_set_log_file(mr_attr_t *attr, const char *path) {
    if (attr == NULL) {
        mr_err("mr_attr_set_log_file: parametro 'attr' è NULL.");
        return -1;
    }

    if (attr->log_file != NULL)
        free((void *)attr->log_file);
    
    attr->log_file = path;
    return 0;
}

int mr_attr_set_hash_function(mr_attr_t *attr, mr_hash_t hash, void *hash_arg) {
    if (attr == NULL) {
        mr_err("mr_attr_set_hash_function: parametro 'attr' è NULL.");
        return -1;
    }
    
    attr->hash = hash;
    attr->hash_arg = hash_arg;
    
    return 0;
}

int mr_set_mapper_pid(mr_t mr, pid_t pid){
    if(mr == NULL) {
        mr_err("mr_set_mapper_pid: mapreducer (mr) nullo.");
        return -1;
    }
    mr->mapper = pid;
    return 0;
}

int mr_set_reducer_pid(mr_t mr, pid_t pid){
    if(mr == NULL) {
        mr_err("mr_set_reducer_pid: mapreducer (mr) nullo.");
        return -1;
    }
    mr->reducer = pid;
    return 0;
}

int mr_set_main_pid(mr_t mr, pid_t pid){
    if(mr == NULL) {
        mr_err("mr_set_main_pid: mapreducer (mr) nullo.");
        return -1;
    }
    mr->main = pid;
    return 0;
}

int mr_destroy(mr_t mr){
    if (mr == NULL) {
        return -1;
    }

    // Libera il file di log se è stato allocato dinamicamente
    if (mr->config.log_file != NULL) {
        free((void *)mr->config.log_file);
        mr->config.log_file = NULL;
    }

    // Libera la struttura principale
    free(mr);
    return 0;
}


