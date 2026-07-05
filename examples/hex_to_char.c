#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

int main(int argc, char *argv[]) {
    FILE *f = stdin;
    if (argc > 1) {
        f = fopen(argv[1], "r");
        if (!f) {
            perror("fopen");
            return 1;
        }
    }

    // Stampa l'intestazione
    int c;
    while ((c = fgetc(f)) != EOF && c != '\n') {
        putchar(c);
    }
    if (c == '\n') putchar('\n');

    int token_length;
    // La formattazione è: token_length,token,res_length,hex_res...
    while (fscanf(f, "%d,", &token_length) == 1) {
        char *token = malloc(token_length + 1);
        if (fread(token, 1, token_length, f) != (size_t)token_length) {
            free(token);
            break;
        }
        token[token_length] = '\0';
        
        // Stampa il token e le sue lunghezze esattamente come in ingresso
        printf("%d,", token_length);
        for(int k=0; k<token_length; k++) {
            putchar(token[k]);
        }
        free(token);

        int res_length;
        // Può esserci un numero variabile di risultati per token
        while (fscanf(f, ",%d,", &res_length) == 1) {
            printf(",%d,", res_length);
            for (int i = 0; i < res_length; i++) {
                char hex[3];
                hex[0] = (char)fgetc(f);
                hex[1] = (char)fgetc(f);
                hex[2] = '\0';
                char decoded = (char)((hex_char_to_int(hex[0]) << 4) | hex_char_to_int(hex[1]));
                putchar(decoded);
            }
            
            // Il prossimo carattere può essere una virgola (altro risultato) o newline (fine record)
            int next = fgetc(f);
            if (next == '\n' || next == EOF) {
                break;
            } else if (next == ',') {
                ungetc(next, f);
            }
        }
        printf("\n");
    }

    if (f != stdin) fclose(f);
    return 0;
}
