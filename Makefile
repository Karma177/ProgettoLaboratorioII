# Makefile for ProgettoLaboratorioII

CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -g
LDFLAGS = -pthread

# Directory di destinazione per i file compilati (nella stessa cartella di word_count.c)
DIST_DIR = examples/example1/dist
DIST_DIR2 = examples/example2/dist

# Definizione dei file oggetto all'interno di dist/
OBJS = $(DIST_DIR)/main_process.o \
       $(DIST_DIR)/mapper.o \
       $(DIST_DIR)/reducer.o \
       $(DIST_DIR)/attributes.o \
       $(DIST_DIR)/logs.o

OBJS2 = $(DIST_DIR2)/main_process.o \
        $(DIST_DIR2)/mapper.o \
        $(DIST_DIR2)/reducer.o \
        $(DIST_DIR2)/attributes.o \
        $(DIST_DIR2)/logs.o

LIB = $(DIST_DIR)/libmr.a
LIB2 = $(DIST_DIR2)/libmr.a

EXAMPLE1 = $(DIST_DIR)/word_count
EXAMPLE2 = $(DIST_DIR2)/word_count_multi

all: $(LIB) $(EXAMPLE1) $(LIB2) $(EXAMPLE2)

$(LIB): $(OBJS)
	ar rcs $@ $^

$(LIB2): $(OBJS2)
	ar rcs $@ $^

# Regole di compilazione per i file sorgente in src/ (Example 1)
$(DIST_DIR)/%.o: src/%.c
	@mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Regole di compilazione per i file sorgente in src/util/ (Example 1)
$(DIST_DIR)/%.o: src/util/%.c
	@mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Regole di compilazione per i file sorgente in src/ (Example 2)
$(DIST_DIR2)/%.o: src/%.c
	@mkdir -p $(DIST_DIR2)
	$(CC) $(CFLAGS) -c $< -o $@

# Regole di compilazione per i file sorgente in src/util/ (Example 2)
$(DIST_DIR2)/%.o: src/util/%.c
	@mkdir -p $(DIST_DIR2)
	$(CC) $(CFLAGS) -c $< -o $@

$(EXAMPLE1): examples/example1/word_count.c $(LIB)
	@mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) $< -L$(DIST_DIR) -lmr $(LDFLAGS) -o $@

$(EXAMPLE2): examples/example2/word_count_multi.c $(LIB2)
	@mkdir -p $(DIST_DIR2)
	$(CC) $(CFLAGS) $< -L$(DIST_DIR2) -lmr $(LDFLAGS) -o $@

test: all
	@echo "=================================================="
	@echo "Esecuzione Test Word Count Singolo (Example 1)..."
	@echo "=================================================="
	./$(EXAMPLE1) examples/example1/input_test test_output.txt
	@echo "Prime 20 righe del risultato (examples/example1/test_output.txt):"
	@tail -n 20 examples/example1/test_output.txt
	@echo ""
	@echo "=================================================="
	@echo "Esecuzione Test Word Count Multiplo Concorrente (Example 2)..."
	@echo "=================================================="
	./$(EXAMPLE2)
	@echo "Prime 10 righe del risultato 1 (examples/example2/test_output_lorem.txt):"
	@tail -n 10 examples/example2/test_output_lorem.txt
	@echo ""
	@echo "Prime 10 righe del risultato 2 (examples/example2/test_output_test1.txt):"
	@tail -n 10 examples/example2/test_output_test1.txt

clean:
	rm -rf $(DIST_DIR) examples/example1/test_output.txt examples/example1/logs/
	rm -rf $(DIST_DIR2) examples/example2/test_output_lorem.txt examples/example2/test_output_test1.txt examples/example2/logs/

.PHONY: all test clean