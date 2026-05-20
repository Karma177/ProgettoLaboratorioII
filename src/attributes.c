#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "../include/mr.h"
#include <time.h>
#include <string.h>

struct mr {
    mr_attr_t config;     // Una copia locale della configurazione (thread, code, log)
    mr_mapper_t mapper_f;   // Il puntatore alla funzione Mapper dell'utente
    mr_reducer_t reducer_f; // Il puntatore alla funzione Reducer dell'utente
    void *user_arg;       // L'argomento opzionale passato dall'utente
    mr_hash_t hash_f;
    pid_t mapper;
    pid_t reducer;
};

// TODO: reindirizzare errno a logger, e dove vi è "return -1" scrivere la causa nei log. 

int mr_setup(){
    mr_attr_t *attributes = NULL;
    if (mr_attr_init(&attributes) == -1 || attributes == NULL)
        return -1;
    mr_attr_set_mapper_threads(attributes, 10);
    mr_attr_set_reducer_threads(attributes, 10);
    mr_attr_set_queue_size(attributes, 20);
    mr_attr_set_log_file(attributes, generate_log_header());
    // TODO: print attributes su logs (e su console)
    //mr_attr_set_hash_function()
    
    return 0;
}

int mr_create(mr_t *mr, const mr_attr_t *attr, mr_mapper_t mapper, mr_reducer_t reducer, void *user_arg){
    if(mr == NULL || attr == NULL || mapper == NULL || reducer == NULL)
        return -1;
    
    mr_t temp = malloc(sizeof(struct mr));
    if(temp == NULL)
        return -1;
    
    temp->config = *attr;
    temp->mapper_f = mapper;
    temp->reducer_f = reducer;
    temp->user_arg = user_arg;
    
    *mr = temp;
    return 0;
}

int mr_attr_init(mr_attr_t **attr){
    mr_attr_t *temp = malloc(sizeof(mr_attr_t));
    if (temp == NULL) {
        return -1;
    }

    temp->mapper_threads = 3;
    temp->reducer_threads = 3;
    temp->queue_size = 10;
    temp->log_file = NULL;
    
    *attr = temp;
    return 0;
}

int mr_attr_destroy(mr_attr_t *attr) {
    if (attr == NULL)
        return -1;

    if (attr->log_file != NULL)
        free((void *)attr->log_file); 
    
    free(attr);
    return 0;
}

int mr_attr_set_mapper_threads(mr_attr_t *attr, size_t n) {
    if (attr == NULL)
        return -1;
    attr->mapper_threads = n;
    return 0;
}

int mr_attr_set_reducer_threads(mr_attr_t *attr, size_t n) {
    if (attr == NULL)
        return -1;
    attr->reducer_threads = n;
    return 0;
}

int mr_attr_set_queue_size(mr_attr_t *attr, size_t n) {
    if (attr == NULL)
        return -1;
    attr->queue_size = n;
    return 0;
}

int mr_attr_set_log_file(mr_attr_t *attr, const char *path) {
    if (attr == NULL)
        return -1;

    if (attr->log_file != NULL)
        free((void *)attr->log_file);
    
    attr->log_file = path;
    return 0;
}

int mr_attr_set_hash_function(mr_attr_t *attr, mr_hash_t hash, void *hash_arg) {
    if (attr == NULL)
        return -1;
    mr_hash_t temp = malloc(sizeof(mr_hash_t));
    // TODO: to be implemented
    
    return 0;
}

char* generate_log_header(){
    time_t now = time(NULL);
    struct tm tm_now;
    char timestr[64];

    if (now == (time_t)-1) {
        return NULL;
    }

    localtime_r(&now, &tm_now);
    if (strftime(timestr, sizeof(timestr), "%Y-%m-%d_%H-%M-%S", &tm_now) == 0) {
        return NULL;
    }

    const char prefix[] = "logs/";
    size_t len = strlen(prefix) + strlen(timestr) + strlen(".log") + 1;
    char *logfile = malloc(len);
    if (logfile == NULL) {
        return NULL;
    }

    strcpy(logfile, prefix);
    strcat(logfile, timestr);
    strcat(logfile, ".log");
    
    return logfile;
}