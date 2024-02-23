# Offline Messenger

## Descriere proiect 
Proiectul Offline Messenger are ca scop crearea unei aplicatii de tip client-server care sa sustina trimiterea de mesaje intre utilizatori. Un utilizator care nu are cont va avea optiunea de a-si crea unul si de a putea schimba apoi mesaje cu alti clienti. Aplicatia ofera posibilitatea de a trimite mesaje si utilizatorilor ce nu sunt activi.

## Caracteristici
+ Arhitectura Client-Server : implementarea unui model client-server care faciliteaza comunicarea dintre utilizatori.
+ Autentificare: Proces de autentificare prin introducerea numelui și parolei.
+ Creare Cont: Posibilitatea de a crea un cont dacă utilizatorul nu are unul.
+ Istoric Conversație: Acces la istoricul conversației curente.
+ Reply la Mesaje: Posibilitatea de a răspunde la orice mesaj din conversația curentă.
+ Notificare Mesaje: Utilizatorul primește mesaje de la alți utilizatori în timp ce interacționează cu altcineva.
+ Thread-uri pentru Conexiuni: Crearea de thread-uri separate pentru a servi fiecare client conectat la server.

## Rulare proiect
Compilare server.c
```
gcc server.c -o server_exec -lpthread - lsqlite3
```
Rulare executabil server_exec
```
./server_exec
```
Compilare client.c (puteti rula+compila intre 1 - 100 clienti)
```
gcc client.c - o client_exec
```
Rulare executabil client_exec
```
./client_exec
```
