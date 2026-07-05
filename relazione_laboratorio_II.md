# Relazione Progetto Laboratorio II: MapReduce in C POSIX

## 1. Introduzione e Obiettivi

Questo progetto ha lo scopo di realizzare in linguaggio C un framework concorrente e multiprocesso che implementi il modello **MapReduce**.

Tutto il codice è stato sviluppato attenendosi allo standard **C11 (`<threads.h>`)** per la gestione del multithreading e allo standard POSIX per l'infrastruttura multiprocesso e la relativa comunicazione.

Gli obiettivi principali del progetto sono stati:
- **Esecuzione in parallelo**: Il carico di lavoro è stato suddiviso istanziando processi separati (tramite `fork()`) per coordinare, mappare e ridurre i dati.
- **Comunicazione sicura tra processi**: I processi comunicano tra loro scambiandosi i dati attraverso le pipe. Al fine di assicurare la consistenza dei dati, abbiamo implementato un protocollo di comunicazione che struttura i messaggi nel formato `{lunghezza_token},{token}` e `{lunghezza_valore},{valore}`.
- **Gestione opaca dei dati**: Il framework è progettato per operare in modo indipendente rispetto alla natura del risultato stabilita dall'utente. Poiché il framework non possiede (e non necessita di) conoscenze semantiche sul dato emesso, esso viene manipolato strettamente come dato opaco. Il risultato finale viene serializzato su file traducendo i byte raw in formato esadecimale, unicamente per facilitarne l'ispezione testuale rispetto al binario puro.
- **Resistenza agli errori**: Alla ricezione del segnale `SIGINT` (es. CTRL+C), il processo principale gestisce la terminazione pulita di tutti i processi figli e la corretta chiusura dei descrittori. Inoltre, l'assenza di memory leak e corruzioni della memoria è stata verificata tramite l'ausilio delle specifiche flag dei compilatori C (come i Sanitizers).

## 2. Interfaccia Pubblica

Il framework è esposto all'utente tramite una libreria statica (`libmr.a`), la cui interfaccia pubblica richiede un'interazione minimale. 
L'utente interagisce con il sistema popolando una struttura dati di configurazione opaca (`mr_t`), all'interno della quale inietta i puntatori a funzione per le proprie logiche:
- `mr_mapper_t`: La callback invocata dai worker del Mapper per processare una specifica riga testuale in ingresso.
- `mr_reducer_t`: La callback invocata dai worker del Reducer per iterare e collassare l'array di risultati accomunati dal medesimo token.

Per l'emissione e il consolidamento dei dati (intermedi e conclusivi), il framework espone all'utente in sola lettura le funzioni `emit_pair` e `emit_result`. Una volta preparata l'istanza `mr_t`, il ciclo di vita dell'intero flusso viene innescato tramite l'entry-point pubblico `mr_start()`, che orchestra in modo isolato i vari step e rilascia nuovamente il controllo all'utente al completamento del task.

## 3. Architettura del Sistema

L'architettura del nostro framework si basa su un pattern "Master-Worker" suddiviso in tre processi principali:
1. **Main Process**
2. **Processo Mapper**
3. **Processo Reducer**

Quando l'esecuzione viene avviata, il Main Process predispone le pipe di comunicazione e istanzia i processi Mapper e Reducer tramite la system call `fork()`. 
Per isolare e instradare i flussi comunicativi in maniera trasparente e invisibile per le logiche applicative, i processi figli impiegano in modo intensivo la chiamata `dup2()`: il Mapper sovrascrive il proprio `STDIN` puntandolo al lato di lettura della prima pipe, e il proprio `STDOUT` verso il lato di scrittura della pipe successiva. Il Reducer si connette a cascata adottando un rimpiazzo dei descrittori analogo.

Mentre Mapper e Reducer restano in ascolto e operano in background, il Main process acquisisce le directory, esegue il parsing dei file di input e invia le singole righe al Mapper. Al termine del trasferimento, il Main utilizza la direttiva `waitpid()` per porre se stesso in attesa della terminazione fisiologica dei figli, raccoglie i risultati aggregati emessi dal Reducer, li persiste sul file di output e infine esegue il cleanup delle risorse.

**La Comunicazione (Pipe)**
Per la comunicazione inter-processo vengono impiegate tre pipe unidirezionali:
- `main_to_mapper`: Il Main invia le singole righe al Mapper.
- `mapper_to_reducer`: Il Mapper invia le coppie processate (`[token, valore]`) al Reducer.
- `reducer_to_main`: Il Reducer invia i risultati finali aggregati al Main.

Per garantire l'integrità dei dati scambiati attraverso le pipe, viene applicato il protocollo binario precedentemente menzionato. Per ogni stringa da inoltrare, il processo mittente provvede a serializzare preventivamente su pipe un intero che ne descrive la dimensione, seguito immediatamente dalla stringa raw.

## 4. Modulo Mapper

Il modulo Mapper è responsabile della prima fase di elaborazione. La sua architettura interna sfrutta un pool di thread gestiti tramite lo standard C11 (`<threads.h>`) per processare concorrentemente le righe di input lette dalla pipe `main_to_mapper`.

### Gestione dell'Input e Thread Pool
L'infrastruttura di lettura delega l'acquisizione dei dati a un thread dedicato (*reader thread*), il cui unico compito è quello di estrarre le righe dalla pipe in ingresso e accodarle all'interno di una coda interna e condivisa.

I worker thread restano in attesa di elementi all'interno di tale coda. La sincronizzazione e la gestione della concorrenza avvengono tramite l'utilizzo combinato di un mutex (`mtx_t`) e di due variabili di condizione (`cnd_t`). Questo permette ai thread di sospendere la propria esecuzione in modo efficiente qualora la coda risulti vuota, risvegliandosi unicamente alla ricezione di un nuovo elemento logico da elaborare.

### Esecuzione ed Emissione
Per ogni riga estratta dalla coda, i worker thread invocano la funzione utente `mr_mapper_t`. Tale funzione applica la logica specifica definita dall'utente, che a sua volta si serve della callback `emit_pair` per ritornare il risultato. 

La funzione `emit_pair`, fornita nativamente dal framework, si occupa di immettere i dati sulla pipe verso il reducer. Questa operazione di scrittura garantisce completa mutua esclusione (mediante `mtx_lock`), impedendo data race e sovrapposizioni nel trasferimento del protocollo binario `{lunghezza_token},{token}`. Questo garantisce un flusso di dati valido e coerente verso il modulo Reducer.

## 5. Modulo Reducer

Il modulo Reducer è incaricato dell'aggregazione e del collasso dei dati emessi dai vari thread del Mapper. Si compone di una fase sequenziale di lettura e raggruppamento e di una successiva fase di riduzione concorrente.

### Strutture Dati e Popolamento della Hash Table
Il thread principale del processo Reducer ascolta dalla pipe `mapper_to_reducer`. A ogni ricezione di una coppia `{token, valore}`, estrae i dati decodificando il protocollo binario e aggiorna una Hash Table interna. Il sistema offre all'utente la possibilità di iniettare una funzione di hashing personalizzata.

Indipendentemente da questa scelta, per gestire in modo efficiente l'aggregazione, sono state implementate specifiche strutture dati interne:
- `ht`: La struttura principale della Hash Table, che tiene traccia della capacità totale e del numero di chiavi.
- `ht_item`: L'elemento della tabella, che memorizza la chiave (`token`) e funge da entry-point per i relativi valori.
- `token_chain` e `result_chain`: Ogni chiave mappa a una `token_chain`, una struttura che descrive una lista concatenata di nodi `result_chain`. Ogni nodo conserva il valore aggregato tramite la struttura generica `mr_value_t`, che incapsula la dimensione e il puntatore ai byte grezzi.

Questo approccio a liste concatenate risolve in modo pulito le collisioni di hash (tecnica del *chaining*) e permette l'inserimento di nuovi valori in tempo costante $O(1)$.
Una volta riscontrata la fine dello stream di input (identificata dall'EOF sulla pipe), il thread principale estrae gli `ht_item` raccolti, li ordina lessicograficamente e li accoda all'interno di una coda condivisa di lavoro, pronta per essere consumata dai thread interni.

### Riduzione Concorrente
Completata la fase di aggregazione, il processo genera un *Thread Pool* di worker. Questi ultimi consumano attivamente gli elementi della coda (ovvero le chiavi univoche e le liste dei loro valori associati) e per ciascuno di essi invocano la funzione utente `mr_reducer_t`.

Il risultato manipolato dalla funzione utente viene quindi inoltrato tramite la callback `emit_result`, la quale trasmette i dati aggregati (utilizzando anch'essa un accesso in mutua esclusione) sulla pipe finale `reducer_to_main`, affinché vengano raccolti e persistiti dal processo Master.

## 6. Main Process e Gestione Output

Il processo principale (Master) agisce come supervisore e orchestratore dell'intero ciclo MapReduce. Oltre alla responsabilità iniziale di istanziare l'infrastruttura delle pipe e generare i processi worker, il Master si occupa attivamente della gestione dei file.

### Lettura Directory e Mappatura File
L'acquisizione dei dati in ingresso avviene tramite una scansione ricorsiva delle directory di input passate dall'utente. Il Master intercetta ogni file idoneo e ne filtra il contenuto riga per riga, popolando per ciascuna di esse una struttura `mr_file_line_t` da immettere in pipe verso il Mapper.

### Sincronizzazione e Scrittura Output
Contemporaneamente alla conclusione delle operazioni dei Mapper e dei Reducer, il Main Process esegue un loop di ascolto passivo sulla pipe terminale `reducer_to_main` per collezionare i risultati conclusivi collassati dal Reducer.

La memorizzazione su disco di questi output finali segue una rigorosa logica indipendente dal formato. Indipendentemente dal payload binario originario emesso dalla logica utente, il Master applica una conversione che traduce individualmente i byte del risultato in una stringa esadecimale. 
Il file risultante adotta un formato posizionale testuale, simile a un CSV, secondo il pattern: 
`[lunghezza_token],[token],[lunghezza_risultato_1],[hex_risultato_1],...`

## 7. Meccanismo di Logging

Per garantire visibilità sull'esecuzione e tracciabilità in fase di debugging, il framework è stato dotato di un modulo di logging (`logs.c`).

- **Variabili Thread-Local**: Il modulo sfrutta la keyword C11 `thread_local` per memorizzare l'istanza globale `mr_t` associata allo specifico thread. Questa astrazione permette di richiamare le funzioni di log da qualsiasi punto della base di codice (anche in profondità nello stack delle chiamate) senza il bisogno di trasportare esplicitamente un puntatore all'istanza originaria tra gli argomenti delle funzioni.
- **Tipi di messaggi e Formattazione**: L'infrastruttura supporta diversi livelli di severità (INFO, ERROR, METRIC, DEBUG). Ciascun evento loggato riporta il timestamp esatto di generazione e identifica attivamente il processo originario (Main, Mapper o Reducer) interrogando a runtime il PID del chiamante. Il formato finale si presenta tipicamente delineato come `[TIMESTAMP] [PROCESSO] [LIVELLO] Messaggio`.
- **Persistenza**: Tutte le scritture su file da parte del modulo sono incapsulate in logiche thread-safe. La destinazione del file di output viene letta direttamente dalla configurazione iniziale e le operazioni sono sicure da eseguire in concorrenza pura.

## 8. Sviluppi Applicativi ed Esempi (Test Suite)

Per validare il framework, il corretto uso della memoria e le performance dell'architettura in scenari operativi verosimili, sono stati scritti esempi d'esecuzione, richiamabili direttamente tramite istruzioni `make`.

- **Example 1 (Word Count base)**: Un test di throughput che calcola le occorrenze delle parole su dataset di svariati Megabyte per misurare i colli di bottiglia su letture disco e accessi Hash Table.
- **Example 2 (Word Count Multiplo)**: Verifica la solidità dell'isolamento dei processi. Esegue istanze parallele multiple dello stesso task, assicurandosi che il framework possa essere smontato e ricostruito più volte all'interno dello stesso programma chiamante senza interferenze.
- **Example 3 (Edge Cases)**: Affronta gli edge case di possibili input erronei dell'utente. Verifica che puntatori nulli e directory inesistenti vengano intercettate, restituendo gli appropriati codici d'errore senza generare `Segmentation Fault` (grazie anche ai controlli estesi dell'AddressSanitizer).
- **Example 4 (Inverted Index)**: Dimostra la flessibilità della funzione Reducer, permettendole di registrare chiavi strutturate e di emettere molteplici occorrenze a fronte dello stesso token originario.

## 9. Conclusioni

La realizzazione di questo progetto ha permesso di consolidare i concetti fondamentali della programmazione di sistema. L'implementazione ha richiesto l'impiego delle primitive POSIX per la gestione dei processi e della comunicazione tramite pipe, unito all'uso dello standard C11 per il multithreading. 

Questo ha portato allo sviluppo di un applicativo robusto, in grado di parallelizzare efficacemente l'esecuzione di task computazionali intensivi. Il corretto utilizzo dei meccanismi di sincronizzazione ha garantito la validità dei risultati e la stabilità del framework, replicando su un singolo elaboratore le dinamiche operative tipiche delle architetture di calcolo distribuito.
