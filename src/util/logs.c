#include <mr_common.h>

char* get_log_file_attr(mr_attr_t attr){
    return attr.log_file;
}

char* get_log_file_mr(mr_t mapreducer){
    if(mapreducer == NULL)
        return NULL;
    return mapreducer->config.log_file;
}

// write_to_log, i log devono essere protetti da mutex!
