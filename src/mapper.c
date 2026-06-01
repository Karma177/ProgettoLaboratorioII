#include "util/mr_common.h"
#include <threads.h>
#include <stdlib.h>
#include <errno.h>

typedef struct Nodo {
    char *file_name;
    size_t file_name_len;
    size_t line_number;
    char *line;
    size_t line_length;
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

mr_t_list* create_list(int queuesize);
node_t* create_node(char* file_name, size_t file_name_len, size_t line_number, char* line, size_t line_length);
int queue(mr_t_list *list, node_t *node);
node_t* dequeue(mr_t_list *list);
int read_item_from_pipe(mr_t_list* coda, int fd_pipe);
int main_listener(mr_t_list* coda, int main_to_mapper);

// Entry point per le funzioni del mapper
int start_mapper(mr_t mr, int main_to_mapper, int mapper_to_reducer){
    mr_t_list* list = create_list(mr->config.queue_size);
    if (list == NULL) {
        mr_err("Impossibile creare la coda del mapper.");
        return -1;
    }

    // TODO: thread reader dal main
    int status = main_listener(list, main_to_mapper);

    return status;
}

int main_listener(mr_t_list* coda, int main_to_mapper){
    if (coda == NULL) {
        return -1;
    }

    int status;
    // legge continuamente dalla pipe e accoda gli elementi
    while ((status = read_item_from_pipe(coda, main_to_mapper)) > 0) {
        // status == 1 indica elemento letto e accodato con successo
    }

    // chiude la coda per segnalare ai thread worker che l'input è finito
    mtx_lock(&coda->mutex);
    coda->closed = 1;
    cnd_broadcast(&coda->empty); // sveglia tutti i thread consumatori
    cnd_broadcast(&coda->full);  // sveglia il thread produttore
    mtx_unlock(&coda->mutex);

    return (status == 0) ? 0 : -1;
}

int read_item_from_pipe(mr_t_list* coda, int fd_pipe){
    int file_name_len;

    // leggiamo la lunghezza del nome del file 
    // readn restituisce 0 == EOF 
    ssize_t res = readn(fd_pipe, &file_name_len, sizeof(int));
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

    if (readn(fd_pipe, file_name, file_name_len) != file_name_len) {
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

    if (queue(coda, node) != 0) {
        // se l'inserimento fallisce, liberiamo la memoria
        free(node->file_name);
        free(node->line);
        free(node);
        return -1;
    }

    return 0;
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

node_t* create_node(char* file_name, size_t file_name_len, size_t line_number, char* line, size_t line_length){
    if (file_name == NULL || file_name_len == 0 || line == NULL){
        mr_err("Errore durante la creazione di un nodo: parametri non validi.");
        return NULL;
    }

    node_t* node = malloc(sizeof(node_t));
    if (node == NULL) {
        mr_err("Errore durante la creazione di un nodo: malloc fallita.");
        return NULL;
    }
    node->file_name = file_name;
    node->file_name_len = file_name_len;
    node->line_number = line_number;
    node->line = line;
    node->line_length = line_length;
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
