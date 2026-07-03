CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -g
LDFLAGS = -pthread
LIB_DIR = src/dist
LIB = $(LIB_DIR)/libmr.a
DIST_DIR = examples/example1/dist
DIST_DIR2 = examples/example2/dist
DIST_DIR3 = examples/example3/dist
INPUT_TEST ?= examples/input_test

LIB_OBJS = $(LIB_DIR)/main_process.o \
           $(LIB_DIR)/mapper.o \
           $(LIB_DIR)/reducer.o \
           $(LIB_DIR)/attributes.o \
           $(LIB_DIR)/logs.o
EXAMPLE1 = $(DIST_DIR)/word_count
EXAMPLE2 = $(DIST_DIR2)/word_count_multi
EXAMPLE3 = $(DIST_DIR3)/edge_cases
PRINT_OUT = $(LIB_DIR)/print_output
all: $(LIB) $(EXAMPLE1) $(EXAMPLE2) $(EXAMPLE3) $(PRINT_OUT)
$(LIB): $(LIB_OBJS)
	ar rcs $@ $^

$(LIB_DIR)/%.o: src/%.c
	@mkdir -p $(LIB_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
	
$(LIB_DIR)/%.o: src/util/%.c
	@mkdir -p $(LIB_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
	
$(EXAMPLE1): examples/example1/word_count.c $(LIB)
	@mkdir -p $(DIST_DIR)
	$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lmr $(LDFLAGS) -o $@
$(EXAMPLE2): examples/example2/word_count_multi.c $(LIB)
	@mkdir -p $(DIST_DIR2)
	$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lmr $(LDFLAGS) -o $@
$(EXAMPLE3): examples/example3/edge_cases.c $(LIB)
	@mkdir -p $(DIST_DIR3)
	$(CC) $(CFLAGS) $< -L$(LIB_DIR) -lmr $(LDFLAGS) -o $@
$(PRINT_OUT): examples/print_output.c
	@mkdir -p $(LIB_DIR)
	$(CC) $(CFLAGS) $< -o $@
test1: all
	@echo "-/ \\-"
	@echo "Esecuzione Test Word Count Singolo (Example 1)..."
	@echo "-/ \\-"
	./$(EXAMPLE1) $(INPUT_TEST) test_output.txt
	@echo "Prime 20 righe del risultato (test_output.txt):"
	@./$(PRINT_OUT) examples/example1/test_output.txt | head -n 20
	@echo "\nStatistiche di esecuzione:"
	@cat examples/example1/test_output.txt.stats
	@echo ""
test2: all
	@echo "-/ \\-"
	@echo "Esecuzione Test Word Count Multiplo Concorrente (Example 2)..."
	@echo "-/ \\-"
	./$(EXAMPLE2) $(INPUT_TEST)
	@echo "Prime 10 righe del risultato 1 (examples/example2/test_output_lorem.txt):"
	@./$(PRINT_OUT) examples/example2/test_output_lorem.txt | head -n 10
	@echo "\nPrime 10 righe del risultato 2 (examples/example2/test_output_test1.txt):"
	@./$(PRINT_OUT) examples/example2/test_output_test1.txt | head -n 10
	@echo ""
test3: all
	@echo "-/ \\-"
	@echo "Esecuzione Test Edge Cases (Example 3)..."
	@echo "-/ \\-"
	./$(EXAMPLE3)
test: test1 test2 test3
read: all
	@if [ -z "$(FILE)" ]; then \
		echo "Esempio d'uso: make read FILE=percorso/al/file.txt"; \
		echo "Leggo il file di default (examples/example1/test_output.txt):"; \
		echo "-/ \\-"; \
		./$(PRINT_OUT) examples/example1/test_output.txt | head -n 30; \
	else \
		./$(PRINT_OUT) $(FILE); \
	fi
clean:
	rm -rf $(LIB_DIR)
	rm -rf $(DIST_DIR) examples/example1/test_output.txt examples/example1/test_output.txt.stats examples/example1/logs/
	rm -rf $(DIST_DIR2) examples/example2/test_output_lorem.txt examples/example2/test_output_lorem.txt.stats examples/example2/test_output_test1.txt examples/example2/test_output_test1.txt.stats examples/example2/logs/
	rm -rf $(DIST_DIR3) examples/example3/test_output_edge.txt examples/example3/test_output_edge.txt.stats examples/example3/logs/
.PHONY: all test test1 test2 test3 clean
.PHONY: all test test1 test2 test3 read clean