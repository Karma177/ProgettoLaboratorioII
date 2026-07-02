#include "util/mr_common.h"
#include <string.h>
#include <threads.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>

typedef struct Nodo {
    mr_file_line_t data;
    struct Nodo *next; 
} node_t;

typedef struct {
    node_t *head;
    node_t *tail;
    size_t size;
    size_t queue_max_size;
    int closed; // Flag (0 o 1) per segnalare quando l'input è finito (EOF)
    
    mtx_t mutex;
    cnd_t full; // CondVar per far aspettare il lettore se la coda è piena
    cnd_t empty; // CondVar per far aspettare i worker se non ci sono elementi
} mr_t_list;

typedef struct {
    mr_t_list *coda;
    int main_to_mapper;
    mr_t mr;
} reader_args_t;

typedef struct {
    mr_t_list *list;
    mr_mapper_t function;
    void *user_arg;        
    mr_t mr;
} worker_args_t;

mr_t_list* create_list(int queuesize);
node_t* create_node(char* file_name, size_t file_name_len, size_t line_number, char* line, size_t line_length);
node_t* dequeue(mr_t_list *list);
void destroy_list(mr_t_list *list);
int queue(mr_t_list *list, node_t *node);
int read_item_from_pipe(mr_t_list* coda, int fd_pipe, node_t** item);
int main_listener(mr_t_list* coda, int main_to_mapper);
int reader_thread_func(void *arg);
int mapper_worker_thread_func(void *arg);
int framework_emit_pair(const char *token, const void *value, size_t value_size, void *emit_arg);



static mtx_t mapper_write_mutex;
static size_t mapper_emitted_pairs = 0;

// Entry point per le funzioni del mapper
int start_mapper(mr_t mr){
    char log_msg[128];
    mapper_emitted_pairs = 0;
    mr_t_list* list = create_list(mr->config.queue_size);
    if (list == NULL) {
        mr_err("Impossibile creare la coda del mapper.");
        return -1;
    }

    if (mtx_init(&mapper_write_mutex, mtx_plain) != thrd_success) {
        mr_err("Impossibile creare il mutex di scrittura del mapper.");
        destroy_list(list);
        return -1;
    }

    #if DEBUG
    mr_debug("Lista creata..");
    #endif

    // Alloca gli argomenti per il thread reader
    reader_args_t *args = malloc(sizeof(reader_args_t));
    if (args == NULL) {
        mr_err("Impossibile allocare gli argomenti del thread reader.");
        destroy_list(list);
        return -1;
    }
    args->coda = list;
    args->main_to_mapper = STDIN_FILENO;
    args->mr = mr;

    thrd_t reader_thread;
    if (thrd_create(&reader_thread, reader_thread_func, args) != thrd_success) {
        mr_err("Impossibile creare il thread reader.");
        free(args);
        destroy_list(list);
        return -1;
    }

    thrd_t *workers = malloc(sizeof(thrd_t) * mr->config.mapper_threads);
    size_t workers_created = 0;
    int success = 0;

    if (workers == NULL) {
        mr_err("Impossibile allocare l'array dei thread worker.");
        success = -1;
    } else {
        worker_args_t w_args;
        w_args.list = list;
        w_args.function = mr->mapper_f;
        w_args.user_arg = mr->user_arg;
        w_args.mr = mr;

        while (workers_created < mr->config.mapper_threads) {
            if (thrd_create(&workers[workers_created], mapper_worker_thread_func, &w_args) != thrd_success) {
                mr_err("Impossibile creare il thread worker.");
                success = -1;
                break;
            }
            workers_created++;
        }
    }

    // in caso di errore liberiamo tutte le risorse
    if (success == -1) {
        mtx_lock(&list->mutex);
        list->closed = 1;
        cnd_broadcast(&list->empty);
        cnd_broadcast(&list->full);
        mtx_unlock(&list->mutex);

        close(STDIN_FILENO);
    }

    // pulizia memoria a termine del processo
    // attesa il thread reader (se creato)
    thrd_join(reader_thread, NULL);

    // attesa thread worker creati
    for (size_t j = 0; j < workers_created; j++) thrd_join(workers[j], NULL);
    if (workers) free(workers);
    close(STDOUT_FILENO);
    destroy_list(list);
    mtx_destroy(&mapper_write_mutex);

    snprintf(log_msg, sizeof(log_msg), "Prodotte %zu coppie dal mapper", mapper_emitted_pairs);
    mr_log_event("METRIC_PAIRS", log_msg);

    return success;
}

int framework_emit_pair(const char *token, const void *value, size_t value_size, void *emit_arg) {
    (void)emit_arg;
    int fd_pipe = STDOUT_FILENO;

    int token_len = strlen(token);
    int value_len = value_size;

    if (token_len <= 0 || token_len > LIMITE_RAGIONEVOLE || value_len < 0 || value_len > LIMITE_RAGIONEVOLE) {
        errno = EINVAL;
        return -1;
    }

    mtx_lock(&mapper_write_mutex);
    mapper_emitted_pairs++;
    // Spediamo nell'ordine del protocollo: len_token, token, len_value, value
    if (writen(fd_pipe, &token_len, sizeof(int)) < 0) {
        mtx_unlock(&mapper_write_mutex);
        return -1;
    }
    if (writen(fd_pipe, token, token_len) < 0) {
        mtx_unlock(&mapper_write_mutex);
        return -1;
    }
    if (writen(fd_pipe, &value_len, sizeof(int)) < 0) {
        mtx_unlock(&mapper_write_mutex);
        return -1;
    }
    if (value_len > 0 && value != NULL) {
        if (writen(fd_pipe, value, value_len) < 0) {
            mtx_unlock(&mapper_write_mutex);
            return -1;
        }
    }
    mtx_unlock(&mapper_write_mutex);

    return 0;
}

int reader_thread_func(void *arg) {
    reader_args_t *args = (reader_args_t *)arg;
    set_log_mr(args->mr);
    mr_log_event("THREAD_START", "Avviato thread reader");
    
    int status = main_listener(args->coda, args->main_to_mapper);
    
    mr_log_event("THREAD_END", "Terminato thread reader");
    free(args); 
    return status;
}

int mapper_worker_thread_func(void *arg){
    worker_args_t *args = (worker_args_t *)arg;
    set_log_mr(args->mr);
    node_t *node;
    mr_log_event("THREAD_START", "Avviato thread mapper worker");
    // Worker logic
    // null se la coda è chiusa o non esiste
    while ((node = dequeue(args->list)) != NULL) {
        args->function(&(node->data), framework_emit_pair, NULL, args->user_arg);
        free((void *)node->data.file_name);
        free((void *)node->data.line);
        free(node);
    }

    mr_log_event("THREAD_END", "Terminato thread mapper worker");
    return 0;
}


int main_listener(mr_t_list* coda, int main_to_mapper){
    if (coda == NULL) {
        mr_err("Coda non inizializzata. Non posso ascoltare sulla pipe.");
        return -1;
    }

    int status;
    node_t* item = NULL;
    // legge continuamente dalla pipe e accoda gli elementi
    while ((status = read_item_from_pipe(coda, main_to_mapper, &item)) > 0) {
        // status == 1 indica elemento letto e accodato con successo
        if(item != NULL){
            queue(coda, item);
            // item=NULL superfluo; teoricamente non è possibile un inserimento doppio del solito item, a patto che
            // read_item_from_pipe ritorni >0 nonostante ci sia stato un errore..
            // guardrail extra
            item = NULL;
        }
    }

    // chiude la coda per segnalare ai thread worker che l'input è finito
    mtx_lock(&coda->mutex);
    coda->closed = 1;
    cnd_broadcast(&coda->empty); // sveglia tutti i thread consumatori
    cnd_broadcast(&coda->full);  // sveglia il thread produttore
    mtx_unlock(&coda->mutex);

    return (status == 0) ? 0 : -1;
}

int read_item_from_pipe(mr_t_list* coda, int fd_pipe, node_t** item){
    (void)coda;
    size_t file_name_len;

    // leggiamo la lunghezza del nome del file 
    // readn restituisce 0 == EOF 
    ssize_t res = readn(fd_pipe, &file_name_len, sizeof(size_t));
    if (res == 0)
        return 0; // EOF regolare

    if (res < 0)
        return -1; // Errore di lettura sulla pipe

    // controllo di validità sulla lunghezza del nome del file (Sezione 10 del testo)
    if (file_name_len <= 0 || file_name_len > LIMITE_RAGIONEVOLE) {
        mr_err("Lunghezza nome file non valida ricevuta dal Main.");
        errno = EBADMSG; // Bad Message
        return -1;
    }

    char *file_name = malloc(file_name_len + 1);
    if (!file_name) {
        return -1;
    }

    if (readn(fd_pipe, file_name, file_name_len) != (ssize_t)file_name_len) {
        free(file_name);
        errno = EPIPE;
        return -1;
    }
    file_name[file_name_len] = '\0'; // Terminatore di sicurezza interna

    // leggiamo il numero di riga
    size_t line_number;
    if (readn(fd_pipe, &line_number, sizeof(size_t)) != sizeof(size_t)) {
        free(file_name);
        errno = EPIPE;
        return -1;
    }

    // leggiamo la lunghezza della riga di testo
    int line_len;
    if (readn(fd_pipe, &line_len, sizeof(int)) != sizeof(int)) {
        free(file_name);
        errno = EPIPE;
        return -1;
    }

    // Controllo di validità sulla lunghezza della riga
    if (line_len < 0 || line_len > LIMITE_RAGIONEVOLE) {
        mr_err("Lunghezza riga non valida ricevuta dal Main.");
        free(file_name);
        errno = EBADMSG;
        return -1;
    }

    // alloco lo spazio e leggo il testo della riga
    char *line = malloc(line_len + 1);
    if (!line) {
        free(file_name);
        return -1;
    }

    if (readn(fd_pipe, line, line_len) != line_len) {
        free(file_name);
        free(line);
        errno = EPIPE;
        return -1;
    }
    line[line_len] = '\0';

    node_t *node = create_node(file_name, file_name_len, line_number, line, line_len);
    if (node == NULL) {
        free(file_name);
        free(line);
        return -1;
    }
    *item = node;
    return 1;
}

mr_t_list* create_list(int queuesize){
    mr_t_list* list = malloc(sizeof(mr_t_list));
    if (list == NULL) return NULL;

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    list->queue_max_size = queuesize;
    list->closed = 0;
    
    if (mtx_init(&list->mutex, mtx_plain) != thrd_success) {
        free(list);
        return NULL;
    }

    if (cnd_init(&list->full) != thrd_success) {
        mtx_destroy(&list->mutex);
        free(list);
        return NULL;
    }

    if (cnd_init(&list->empty) != thrd_success) {
        cnd_destroy(&list->full);
        mtx_destroy(&list->mutex);
        free(list);
        return NULL;
    }

    return list;
}

void destroy_list(mr_t_list *list) {
    if (list == NULL) return;

    node_t *curr = list->head;
    while (curr != NULL) {
        node_t *temp = curr;
        curr = curr->next;
        if (temp->data.file_name) free((void *)temp->data.file_name);
        if (temp->data.line) free((void *)temp->data.line);
        free(temp);
    }

    cnd_destroy(&list->full);
    cnd_destroy(&list->empty);
    mtx_destroy(&list->mutex);
    free(list);
}


node_t* create_node(char* file_name, size_t file_name_len, size_t line_number, char* line, size_t line_length){
    if (file_name == NULL || file_name_len == 0 || line == NULL){
        mr_err("Errore durante la creazione di un nodo: parametri non validi.");
        return NULL;
    }

    node_t* node = malloc(sizeof(node_t));
    if (node == NULL) return NULL;

    node->data.file_name = file_name;
    node->data.file_name_len = file_name_len;
    node->data.line_number = line_number;
    node->data.line = line;
    node->data.line_len = line_length;

    node->next = NULL;
    return node;
}

int queue(mr_t_list *list, node_t *node){
    if (list == NULL || node == NULL){
        mr_err("Impossibile aggiungere un nodo in lista (coda o nodo non validi).");
        return -1;   
    }

    mtx_lock(&list->mutex);

    // se la coda è piena e la pipe non è ancora chiusa (input aperto)
    while (list->size >= list->queue_max_size && !list->closed) {
        cnd_wait(&list->full, &list->mutex);
    }

    // se la coda è stata chiusa
    if (list->closed) {
        mtx_unlock(&list->mutex);
        return -1;
    }

    if (list->head == NULL){
        list->head = node;
        list->tail = node;
    } else {
        list->tail->next = node;
        list->tail = node;
    }
    list->size++;

    cnd_signal(&list->empty);

    mtx_unlock(&list->mutex);
    return 0;
}

node_t* dequeue(mr_t_list *list) {
    if (list == NULL) return NULL;

    mtx_lock(&list->mutex);

    // se la coda è vuota e la pipe non è ancora chiusa (input aperto)
    while (list->head == NULL && !list->closed) {
        cnd_wait(&list->empty, &list->mutex);
    }
    
    if (list->head == NULL && list->closed) {
        mtx_unlock(&list->mutex);
        return NULL;
    }

    node_t *node = list->head;
    list->head = list->head->next;
    if (list->head == NULL) {
        list->tail = NULL;
    }
    list->size--;

    cnd_signal(&list->full);

    mtx_unlock(&list->mutex);
    return node;
}
