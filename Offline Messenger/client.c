#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>

extern int errno;      // codul de eroare returnat de anumite apeluri
int port;              // portul de conectare la server
int sd;                // descriptorul de socket
int trimite_mesaj = 1; // am nevoie in ambele thread uri

typedef struct thData
{
    int idThread; // id-ul thread-ului tinut in evidenta de acest program
    int cl;       // descriptorul intors de accept
    char nume_client[256];
    char nume_al_doilea_participant[256];
} thData;

static void *primul_thread(void *);
void citire(void *);
static void *al_doilea_thread(void *);
void scriere(void *);

void sigintHandler(int sig_num)
{
    printf("\nReceived Ctrl+C signal. Closing socket and exiting.\n");
    close(sd);
    exit(0);
}

int main(int argc, char *argv[])
{
    struct sockaddr_in server; // structura folosita pentru conectare
    int stop = 0, dimensiune;
    char buf[1000], raspuns[1000], cont[256], doriti_cont[256], nume[256], al_doilea_participant[256];
    int login1 = 0, login2 = 0;
    pthread_t th1[10]; // Identificatorii thread-urilor care se vor crea
    pthread_t th2[10]; // Identificatorii thread-urilor care se vor crea
    int i = 0, j = 0;

    // in caz de ctrl+c se va trimite semnal
    signal(SIGINT, sigintHandler);

    /* exista toate argumentele in linia de comanda? */
    if (argc != 3)
    {
        printf("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
        return -1;
    }

    /* stabilim portul */
    port = atoi(argv[2]);

    /* cream socketul */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Eroare la socket().\n");
        return errno;
    }

    /* umplem structura folosita pentru realizarea conexiunii cu serverul */
    /* familia socket-ului */
    server.sin_family = AF_INET;
    /* adresa IP a serverului */
    server.sin_addr.s_addr = inet_addr(argv[1]);
    /* portul de conectare */
    server.sin_port = htons(port);

    /* ne conectam la server */
    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[client]Eroare la connect().\n");
        return errno;
    }

    // while(!stop)
    //{
    memset(nume, 0, sizeof(nume));
    memset(al_doilea_participant, 0, sizeof(al_doilea_participant));
    memset(cont, 0, sizeof(cont));
    memset(doriti_cont, 0, sizeof(doriti_cont));
    login1 = 0;
    login2 = 0;

    // Aveti cont?
    memset(buf, 0, sizeof(buf));

    printf("[client]Aveti cont? : ");
    fflush(stdout);

    scanf("%[^\n]", buf);
    while (getchar() != '\n')
        ;

    strcpy(cont, buf);

    if (write(sd, &buf, sizeof(buf)) <= 0)
    {
        perror("[client]Eroare la write() spre server.\n");
        return errno;
    }

    if (strcmp(buf, "nu") == 0) // intra pe cazul nu am cont
    {
        // Doriti cont?
        memset(buf, 0, sizeof(buf));

        printf("[client]Doriti sa va faceti cont? : ");
        fflush(stdout);

        scanf("%[^\n]", buf);
        while (getchar() != '\n')
            ;

        strcpy(doriti_cont, buf);

        if (write(sd, &buf, sizeof(buf)) <= 0)
        {
            perror("[client]Eroare la write() spre server.\n");
            return errno;
        }

        if (strcmp(buf, "nu") == 0) // nu am cont si nici nu vr sa mi fac cont -> Bye
        {
            stop = 1;
        }
    }

    if (!stop) // intra pe cazurile : 1. am cont si vr sa ma conectez 2. nu am cont dar vr sa mi fac cont
    {
        if (strcmp(cont, "da") == 0) // 1
        {
            // trimis nume
            memset(buf, 0, sizeof(buf));

            printf("[client]Introduceti un nume: ");
            fflush(stdout);

            scanf("%[^\n]", buf);
            while (getchar() != '\n')
                ;
            strcpy(nume, buf);

            if (strcmp(buf, "quit") == 0)
            {
                stop = 1;
            }

            if (write(sd, &buf, sizeof(buf)) <= 0)
            {
                perror("[client]Eroare la write() spre server.\n");
                return errno;
            }

            // trimis parola
            memset(buf, 0, sizeof(buf));

            printf("[client]Introduceti o parola: ");
            fflush(stdout);

            scanf("%[^\n]", buf);
            while (getchar() != '\n')
                ;

            if (strcmp(buf, "quit") == 0)
            {
                stop = 1;
            }

            if (write(sd, &buf, sizeof(buf)) <= 0)
            {
                perror("[client]Eroare la write() spre server.\n");
                return errno;
            }

            // citirea raspunsului dat de server -> verificare asociere
            if (read(sd, &raspuns, sizeof(raspuns)) <= 0)
            {
                perror("[client]Eroare la read() de la server.\n");
                return errno;
            }

            if (strcmp(raspuns, "Asociere incorecta!") == 0)
            {
                stop = 1;

                printf("[client]Mesajul primit este: %s\n", raspuns);

                memset(raspuns, 0, sizeof(raspuns));
            }
            else if (strcmp(raspuns, "Asociere corecta! Te ai logat!") == 0)
            {
                login1 = 1;

                printf("[client]Mesajul primit este: %s\n", raspuns);

                memset(raspuns, 0, sizeof(raspuns));

                // read pentru a afla daca sunt sau nu mesaje necitite de catre utilizator
                if (read(sd, &raspuns, sizeof(raspuns)) <= 0)
                {
                    perror("[client]Eroare la read() de la server.\n");
                    return errno;
                }

                printf("[client]%s\n", raspuns);
            }

            memset(raspuns, 0, sizeof(raspuns));
        }
        else if (strcmp(cont, "nu") == 0 && strcmp(doriti_cont, "da") == 0) // 2
        {
            // trimis nume
            memset(buf, 0, sizeof(buf));

            printf("[client]Introduceti un nume: ");
            fflush(stdout);

            scanf("%[^\n]", buf);
            while (getchar() != '\n')
                ;

            strcpy(nume, buf);

            if (strcmp(buf, "quit") == 0)
            {
                stop = 1;
            }

            if (write(sd, &buf, sizeof(buf)) <= 0)
            {
                perror("[client]Eroare la write() spre server.\n");
                return errno;
            }

            // trimis parola
            memset(buf, 0, sizeof(buf));

            printf("[client]Introduceti o parola: ");
            fflush(stdout);

            scanf("%[^\n]", buf);
            while (getchar() != '\n')
                ;

            if (strcmp(buf, "quit") == 0)
            {
                stop = 1;
            }

            if (write(sd, &buf, sizeof(buf)) <= 0)
            {
                perror("[client]Eroare la write() spre server.\n");
                return errno;
            }

            // citirea raspunsului dat de server -> verificare daca numele dat exista deja in baza de date
            if (read(sd, &raspuns, sizeof(raspuns)) <= 0)
            {
                perror("[client]Eroare la read() de la server.\n");
                return errno;
            }

            // afisam mesajul primit
            if (strcmp(raspuns, "Utilizator adaugat! Te ai logat!") == 0)
            {
                printf("[client]Mesajul primit este: %s\n", raspuns);
                login2 = 1;
            }
            else if (strcmp(raspuns, "Numele se afla deja in baza de date! Nu poti sa ti creezi cont cu acest nume!") == 0)
            {
                stop = 1;
                printf("[client]Mesajul primit este: %s\n", raspuns);
            }

            memset(raspuns, 0, sizeof(raspuns));
        }

        if (login1 == 1 || login2 == 1)
        {
            // introducere numele celui de al doilea participant
            memset(buf, 0, sizeof(buf));

            printf("[client]Introduceti numele persoanei cu care vreti sa conversati : ");
            fflush(stdout);

            scanf("%[^\n]", buf);
            while (getchar() != '\n')
                ;

            strcpy(al_doilea_participant, buf);

            if (strcmp(buf, "quit") == 0)
            {
                stop = 1;
            }

            if (write(sd, &buf, sizeof(buf)) <= 0)
            {
                perror("[client]Eroare la write() spre server.\n");
                return errno;
            }

            memset(buf, 0, sizeof(buf));

            // serverul trimite raspuns daca in urma verificarii al 2 lea participant apartine bd
            // daca da -> ce e mai jos
            // daca nu -> stop = 1

            if (read(sd, &raspuns, sizeof(raspuns)) <= 0)
            {
                perror("[client]Eroare la read() de la server.\n");
                return errno;
            }

            printf("[client]Mesajul primit este: %s\n", raspuns);

            if (strcmp(raspuns, "Acest participant exista in baza de data.") == 0)
            {
                // serverul trimite raspuns daca e activ sau nu al doilea participant

                // read aici pt raspuns
                memset(raspuns, 0, sizeof(raspuns));

                if (read(sd, &raspuns, sizeof(raspuns)) <= 0)
                {
                    perror("[client]Erroare la read() de la server.\n");
                    return errno;
                }

                printf("[client]Mesajul primit este: %s\n", raspuns);

                // aici creez 2 thread uri
                // primul -> citire de pe socket + afisare
                // al doilea -> scanf pt citire mesaj de la tastatura + scriere pe socket

                thData *td1, *td2;

                td1 = (struct thData *)malloc(sizeof(struct thData));
                td2 = (struct thData *)malloc(sizeof(struct thData));

                td1->idThread = i++;
                td1->cl = sd;
                strcpy(td1->nume_client, nume);
                strcpy(td1->nume_al_doilea_participant, al_doilea_participant);

                pthread_create(&th1[i], NULL, &primul_thread, td1);

                td2->idThread = j++;
                td2->cl = sd;
                strcpy(td2->nume_client, nume);
                strcpy(td2->nume_al_doilea_participant, al_doilea_participant);

                pthread_create(&th2[j], NULL, &al_doilea_thread, td2);

                while (trimite_mesaj)
                {
                }
            }
            else if (strcmp(raspuns, "Acest participant nu exista in baza de date.") == 0)
            {
                printf("%s\n", raspuns);
                stop = 1;

                // eventual sa aleaga alt utilizator din bd
                //->sa i se afiseze lista de utilizatori si sa aleaga pe unul, daca nu vrea sa
                //   aleaga pe altcineva da quit si zice Bye
            }
        }
    }
    // inchidem conexiunea, am terminat
    close(sd);
    //}
}

static void *primul_thread(void *arg)
{
    struct thData tdL1;
    tdL1 = *((struct thData *)arg);
    // printf ("[thread %d] - [primul_thread]Asteptam mesajul...\n", tdL1.idThread);
    fflush(stdout);
    pthread_detach(pthread_self());
    citire((struct thData *)arg);
    /* am terminat cu acest client, inchidem conexiunea */
    close((int *)arg);
    return (NULL);
};

static void *al_doilea_thread(void *arg)
{
    struct thData tdL2;
    tdL2 = *((struct thData *)arg);
    // printf ("[thread %d] - [al doilea thread]Asteptam mesajul...\n", tdL2.idThread);
    fflush(stdout);
    pthread_detach(pthread_self());
    scriere((struct thData *)arg);
    /* am terminat cu acest client, inchidem conexiunea */
    close((int *)arg);
    return (NULL);
};

// citire de pe socket
void citire(void *arg)
{
    char raspuns[1000], identitate[256];

    struct thData tdL1;

    tdL1 = *((struct thData *)arg);

    while (trimite_mesaj)
    {
        // printf("trimite_mesaj = %d\n", trimite_mesaj);
        if (read(tdL1.cl, identitate, sizeof(identitate)) <= 0)
        {
            printf("Server-ul a inchis conexiunea!\n");
            exit(0);
            //   return errno;
        }

        if (read(sd, &raspuns, sizeof(raspuns)) <= 0)
        {
            printf("Server-ul a inchis conexiunea!\n");
            exit(0);
            //   return errno;
        }

        if (strcmp(raspuns, "exit1") == 0)
        {
            // printf("am primit exit1\n");
            trimite_mesaj = 0;
        }
        else
        {
            printf("%s : %s\n", identitate, raspuns);
        }

        memset(identitate, 0, sizeof(identitate));
        memset(raspuns, 0, sizeof(raspuns));
    }
}

// scanf pt ceea ce se scrie de la tastatura + scriere pe socket catre server
void scriere(void *arg)
{
    char buf[1000], raspuns[1000];
    struct thData tdL2;

    tdL2 = *((struct thData *)arg);

    while (trimite_mesaj)
    {
        // citire mesaj de la tastatura

        // printf("%s : ", tdL2.nume_client);
        // fflush(stdout);

        scanf("%[^\n]", buf);
        while (getchar() != '\n')
            ;

        if (strcmp(buf, "istoric") == 0)
        {
            // trimit comanda la server
            if (write(tdL2.cl, &buf, sizeof(buf)) <= 0)
            {
                perror("[client]Eroare la write() spre server.\n");
                return errno;
            }

            // read pentru a afisa istoricul
            memset(raspuns, 0, sizeof(raspuns));

            if (read(tdL2.cl, &raspuns, sizeof(raspuns)) <= 0)
            {
                // perror ("[client]Eroare la read() de la server.\n");
                printf("Server-ul a inchis conexiunea!\n");
                exit(0);
                // return errno;
            }

            printf("[client]ISTORIC MESAJE: \n%s\n", raspuns);
        }

        if (strcmp(buf, "exit1") == 0)
        {
            trimite_mesaj = 0;
        }

        if (strcmp(buf, "istoric") != 0)
        {

            // write pentru a trimite mesajul catre server
            if (write(tdL2.cl, &buf, sizeof(buf)) <= 0)
            {
                perror("[client]Eroare la write() spre server.\n");
                return errno;
            } // printf("s a trimis : %s\n", buf);
        }

        memset(buf, 0, sizeof(buf));
        memset(raspuns, 0, sizeof(raspuns));
    }
    close(tdL2.cl);
}
