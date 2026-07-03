#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <file_di_output_binario>\n", argv[0]);
        return 1;
    }

    const char *filepath = argv[1];
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        perror("Errore nell'apertura del file");
        return 1;
    }

    int token_len;
    while (fread(&token_len, sizeof(int), 1, f) == 1) {
        if (token_len <= 0) {
            fprintf(stderr, "Errore: token_len <= 0 (%d)\n", token_len);
            break;
        }

        char *token = malloc(token_len + 1);
        if (!token) break;
        if (fread(token, 1, token_len, f) != (size_t)token_len) {
            free(token);
            break;
        }
        token[token_len] = '\0';

        printf("%s: ", token);

        while (1) {
            int result_len;
            if (fread(&result_len, sizeof(int), 1, f) != 1) {
                break;
            }

            if (result_len < 0) {
                fprintf(stderr, "Errore: result_len < 0 (%d)\n", result_len);
                break;
            }

            unsigned char *result = NULL;
            if (result_len > 0) {
                result = malloc(result_len);
                if (!result) break;
                if (fread(result, 1, result_len, f) != (size_t)result_len) {
                    free(result);
                    break;
                }
            }

            if (result && result_len > 0) {
                fwrite(result, 1, result_len, stdout);
            }
            if (result) free(result);

            // Controlla se il prossimo byte è '\n'
            int c = fgetc(f);
            if (c == '\n' || c == EOF) {
                break;
            } else {
                printf(" "); // Spazio tra risultati multipli
                ungetc(c, f);
            }
        }
        printf("\n");

        free(token);
    }

    fclose(f);
    return 0;
}
