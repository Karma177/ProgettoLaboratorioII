# Progetto Laboratorio II - MapReduce Framework

## Descrizione del Processo

Il framework è costruito interamente in linguaggio C sfruttando lo standard **C11 (`<threads.h>`)** per il multithreading e le **pipe POSIX** per la comunicazione interprocesso. La logica è gestita da `main_process.c`, che inizializza la pipeline e i pool di thread.

1. **Main Process (`main_process.c`)**:
   - Analizza la directory di input passata dall'utente e genera stream di righe testuali incapsulate in strutture `mr_file_line_t`.
   - Smista il lavoro scrivendo questi dati grezzi in una pipe dedicata (`main_to_mapper`).
   - Inizializza pipe dedicate per il reducer (`mapper_to_reducer`) e pipe di raccolta risultati (`reducer_to_main`).
   - Alla fine dell'esecuzione si mette in ascolto tramite la funzione `listen_to_reducer()` sulla pipe di output, decodificando il formato esadecimale e trascrivendo su file il testo di output.

2. **Mapper (`mapper.c`)**:
   - `N` thread concorrenti restano in attesa leggendo dalla pipe `main_to_mapper`. 
   - Una volta ricevuta la riga, invocano il puntatore alla funzione utente `mr_mapper_t` per ottenere token e valori.
   - Ogni volta che la funzione utente chiama il callback `emit_pair`, il mapper interno si occupa di impacchettare la coppia e scriverla in modo thread-safe nella pipe di uscita.
   - Tutti i risultati emessi dai vari mapper convergono poi verso i Reducer, i quali gestiranno il partizionamento e il raggruppamento vero e proprio.

3. **Reducer (`reducer.c`)**:
   - `M` thread in ascolto sull'unica pipe condivisa `mapper_to_reducer` da cui il thread principale del reducer popola la hash table interna, con risoluzione delle collisioni via *chaining* e capacità ottimizzata a `HT_CAPACITY 4099`.
   - La tabella aggrega tutti i valori (`token_chain`) sotto un unico identificativo del `token`.  
   - Al ricevimento del segnale globale di fine inserimento, i thread scorrono la hash table invocando per ogni token la funzione utente `mr_reducer_t`, passandole la chiave e un array `mr_value_t*` con tutti i valori aggregati.
   - Il risultato dell'utente viene emesso dalla callback `emit_result` verso la pipe finale `reducer_to_main` che torna al main process.

### Formato di Output (con risultato esadecimale)

Il formato generato in output segue il template:
`[lunghezza_token],[token_string],[lunghezza_risultato1],[hex_risultato1]...[lunghezza_risultatoN],[hex_risultatoN]`

- I byte raw risultanti emessi dal reducer sono stampati convertendoli esplicitamente in codifica **Esadecimale**.

---

## Comandi del Makefile

Il progetto mette a disposizione un `Makefile` per la compilazione della libreria, degli eseguibili associati e per l'avvio del framework con test inclusi.

### Comandi base
- `make all`: Compila la libreria base MapReduce (`libmr.a`), tutti i programmi utente predefiniti negli `examples/` e il programma di decodifica `hex_to_char`.
- `make clean`: Pulisce tutti i binari generati, i test logs, le directory `dist/` e i file `*.stats`.

### Suite di Test
Il progetto è dotato di quattro esempi integrati. Per testare il framework si possono eseguire comodamente da make:

- `make test1`: Avvia l'Example 1. Un conteggio base delle parole che lavora sulla directory di default (`examples/input_test`).
- `make test2`: Avvia l'Example 2. Esegue molteplici istanze di MapReduce in contemporanea per testare il parallelismo su diverse sottocartelle.
- `make test3`: Avvia l'Example 3. Esegue scenari di **Edge Cases** testando parametri fallati, gestioni difettose della memoria e input malformati per garantire la stabilità della gestione errori.
- `make test4`: Avvia l'Example 4. Implementa un **Inverted Index**, testando specificamente l'emissione iterativa di molteplici valori per lo stesso token e il caricamento del path/numero di riga nativo.

Puoi eseguire in parallelo l'intera suite con il comando:
- `make test`: Equivalente ad eseguire sequenzialmente i test da 1 a 4.

### Flags e Configurazioni
I comandi `make test` accettano flag opzionali che modulano il comportamento del framework in avvio:

- `STRESS=1` *(es. `make test1 STRESS=1`)*: Invece di far leggere il consueto `examples/input_test`, reindirizza l'intero input sulla mole gigantesca di file presenti in `examples/stress_test` (ideale per verificare sovraccarichi della coda IPC e limiti del sistema).
- `TOCHAR=1` *(es. `make test4 TOCHAR=1`)*: Passa in automatico l'output esadecimale illegibile dal tool integrato `hex_to_char`. Il tool formatta dal vivo la decodifica dell'hex a schermo lasciando però immutata l'intera impalcatura del formato output.

### Strumenti e Lettura manuale
Oltre che nel test, è possibile estrapolare con comodo formattazioni raw tramite il target di read:
- `make read FILE=percorso/file.txt`: Legge a schermo intero il file passato come argomento.
- `make read FILE=percorso/file.txt TOCHAR=1`: Decodifica l'hex del file con il programma C personalizzato. *(Nota: se si omette `FILE`, viene caricato di default `examples/example1/test_output.txt` tagliato a 30 righe)*.
