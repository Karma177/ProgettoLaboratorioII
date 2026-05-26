#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include "mr_common.h"



// TODO: reindirizzare errno a logger, e dove vi è "return -1" o NULL scrivere la causa nei log. 

mr_attr_t* mr_attr_setup(size_t mapthreads, size_t reducerthreads, size_t queuesize, char* logfile){
    mr_attr_t *attributes = malloc(sizeof(mr_attr_t)); 
    if (attributes == NULL || mr_attr_init(attributes) == -1)
        return NULL;
    mr_attr_set_mapper_threads(attributes, mapthreads);
    mr_attr_set_reducer_threads(attributes, reducerthreads);
    mr_attr_set_queue_size(attributes, queuesize);
    if(logfile == NULL)
        logfile = generate_log_header();
    mr_attr_set_log_file(attributes, logfile);
    // TODO: print attributes su logs (e su console)
    
    return attributes;
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

int mr_attr_init(mr_attr_t *attr){
    if (attr == NULL) {
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
    
    attr->hash = hash;
    attr->hash_arg = hash_arg;
    
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

int mr_set_mapper_pid(mr_t mr, pid_t pid){
    if(mr == NULL)
        return -1;
    mr->mapper = pid;
    return 0;
}

int mr_set_reducer_pid(mr_t mr, pid_t pid){
    if(mr == NULL)
        return -1;
    mr->reducer = pid;
    return 0;
}

int mr_set_main_pid(mr_t mr, pid_t pid){
    if(mr == NULL)
        return -1;
    mr->main = pid;
    return 0;
}

