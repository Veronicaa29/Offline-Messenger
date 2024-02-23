#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include "sqlite3.h"

#define PORT 2908

extern int errno; //codul de eroare returnat de anumite apeluri 
sqlite3* baza_de_date;
char* eroare = NULL;
pthread_mutex_t mutex_info_client = PTHREAD_MUTEX_INITIALIZER;
int sd;	//descriptorul de socket 

typedef struct thData{
	int idThread; //id-ul thread-ului tinut in evidenta de acest program
	int cl; //descriptorul intors de accept
  char nume_client[256]; //nume client
}thData;

//TREBUIE mutex pentru vectorul asta
thData info_client[100];

static void *treat(void *); /* functia executata de fiecare thread ce realizeaza comunicarea cu clientii */
void raspunde(void *);

void sigintHandler(int sig_num) 
{
    printf("\nReceived Ctrl+C signal. Closing socket and exiting.\n");
    close(sd);
    exit(0);
}

void initializare_info_client()
{
  pthread_mutex_lock(&mutex_info_client);

  for(int i = 0; i < 100; i++)
  {
    info_client[i].cl = -1;
    info_client[i].idThread = -1;
    strcpy(info_client[i].nume_client, "");
  }

  pthread_mutex_unlock(&mutex_info_client);
}

//adaugare client in vectorul info_client[]
void actualizare_info_client(char* nume, int d, int id)
{
  pthread_mutex_lock(&mutex_info_client);

  for(int i = 0; i < 100; i++)
  {
    if(info_client[i].cl == -1)
    {
      strcpy(info_client[i].nume_client, nume);
      info_client[i].cl = d;
      info_client[i].idThread = id;
      break;
    }
  }

  pthread_mutex_unlock(&mutex_info_client);
}

//stergere client din vectorul info_client[]
void actualizare_info_client_2(char* nume)
{
  pthread_mutex_lock(&mutex_info_client);

  for(int i = 0; i < 100; i++)
  {
    if(strcmp(info_client[i].nume_client, nume) == 0)
    {
      strcpy(info_client[i].nume_client, "");
      info_client[i].cl = -1;
      info_client[i].idThread = -1;
    }
  }

  pthread_mutex_unlock(&mutex_info_client);
}

void afisare_info_client()
{
  pthread_mutex_lock(&mutex_info_client);

  for(int i = 0; i < 100; i++)
  {
    if(info_client[i].cl != -1)
    {
      printf("--id : %d, descriptor : %d, nume : %s\n", info_client[i].idThread, info_client[i].cl, info_client[i].nume_client);
    }
  }

  pthread_mutex_unlock(&mutex_info_client);
}

int verificare_activitate(char* str)
{
  int activitate;
  sqlite3_stmt *stmt5;
  char comanda_verificare_activitate[256];

  snprintf(comanda_verificare_activitate, sizeof(comanda_verificare_activitate), "SELECT ACTIVITATE FROM UTILIZATORI WHERE NUME = '%s'", str);

  if(sqlite3_prepare_v2(baza_de_date, comanda_verificare_activitate, -1, &stmt5, 0) != SQLITE_OK)
  {
    printf("Eroare la verificare utilizator : %s!\n", sqlite3_errmsg(baza_de_date));
    sqlite3_free(eroare);
    sqlite3_close(baza_de_date);
    return 3;
  }
          
  if(sqlite3_step(stmt5) == SQLITE_ROW)
  {
    activitate = sqlite3_column_int(stmt5, 0);
  }

  sqlite3_finalize(stmt5);
  memset(comanda_verificare_activitate, 0, sizeof(comanda_verificare_activitate));

  return activitate;
}

void verificare_activitate_clienti()
{
  int ok = 0;
  sqlite3_stmt *stmt18;
  char comanda_extragere_useri_activi[256], utilizatori[256];

  memset(utilizatori, 0, sizeof(utilizatori));

  snprintf(comanda_extragere_useri_activi, sizeof(comanda_extragere_useri_activi), "SELECT NUME FROM UTILIZATORI WHERE ACTIVITATE = 1");

  if(sqlite3_prepare_v2(baza_de_date, comanda_extragere_useri_activi, -1, &stmt18, 0) != SQLITE_OK)
  {
    printf("Eroare la extragere useri activi : %s!\n", sqlite3_errmsg(baza_de_date));
    sqlite3_free(eroare);
    sqlite3_close(baza_de_date);
    return 3;
  }

  while(sqlite3_step(stmt18) == SQLITE_ROW)
  {
    strcat(utilizatori, sqlite3_column_text(stmt18, 0));
    strcat(utilizatori, "\n");
  }

  sqlite3_finalize(stmt18);

  printf("utilizatori activi inainte de verificare: %s\n", utilizatori);

  char *utilizator = strtok(utilizatori, "\n");

  pthread_mutex_lock(&mutex_info_client);

  while(utilizator != NULL)
  {
    printf("**utilizator : %s\n", utilizator);
    for(int i = 0; i < 100; i++)
    {
      if(strcmp(info_client[i].nume_client, utilizator) == 0 )
      {
        ok = 1;
        break;
      }
    }

    if(!ok)
    {
      //schimbare activitate in 0 in baza de date

      char comanda_modificare_activitate[256];
            
      snprintf(comanda_modificare_activitate, sizeof(comanda_modificare_activitate), "UPDATE UTILIZATORI SET ACTIVITATE = 0 WHERE NUME = '%s'", utilizator);

      if(sqlite3_exec(baza_de_date, comanda_modificare_activitate, 0, 0, NULL) != SQLITE_OK)
      {
        printf("Eroare la modificare activitate : %s!\n", sqlite3_errmsg(baza_de_date));
        sqlite3_free(eroare);
        sqlite3_close(baza_de_date);
        return 2;
      }
            
      memset(comanda_modificare_activitate, 0, sizeof(comanda_modificare_activitate));
    }
    else
    {
      //reinitializare ok
      ok = 0;
    }

    utilizator = strtok(NULL, "\n");
  }

  pthread_mutex_unlock(&mutex_info_client);

  memset(utilizatori, 0, sizeof(utilizatori));
  memset(comanda_extragere_useri_activi, 0, sizeof(comanda_extragere_useri_activi));
}

int setare_mesaj_id(int conv_id)
{
  int mesaj_id = 0;

  //caut max(mesaj_id) 
  char comanda_mesaj_id_max[256];
  sqlite3_stmt *stmt8; 

  snprintf(comanda_mesaj_id_max, sizeof(comanda_mesaj_id_max), "SELECT MAX(MESAJ_ID) FROM MESAJE WHERE CONV_ID = '%d'", conv_id);

  if(sqlite3_prepare_v2(baza_de_date, comanda_mesaj_id_max, -1, &stmt8, 0) != SQLITE_OK)
  {
    printf("Eroare la verificare utilizator : %s!\n", sqlite3_errmsg(baza_de_date));
    sqlite3_free(eroare);
    sqlite3_close(baza_de_date);
    return NULL;
  }

  if(sqlite3_step(stmt8) == SQLITE_ROW)
  {
    mesaj_id = sqlite3_column_int(stmt8, 0) + 1;
  }
  else
  {
    mesaj_id = 1;
  }

  sqlite3_finalize(stmt8);
  memset(comanda_mesaj_id_max, 0, sizeof(comanda_mesaj_id_max));

  return mesaj_id;
}

int setare_destinatie(char* str)
{
  int x, ok = 0;

  pthread_mutex_lock(&mutex_info_client);

  for(int i = 0; i < 100; i++)
  {
    if(info_client[i].cl != -1 && strcmp(info_client[i].nume_client, str) == 0)
    {
      x = info_client[i].cl;
      ok = 1;
      break;
    }
  }

  pthread_mutex_unlock(&mutex_info_client);

  if(ok) return x;
  else return -1;
}

int main ()
{
  struct sockaddr_in server;	// structura folosita de server
  struct sockaddr_in from;	
  int nr;		//mesajul primit de trimis la client 
  int pid;
  pthread_t th[100];    //Identificatorii thread-urilor care se vor crea
	int i = 0;
  //int nr_clienti_conectati = 0;

  initializare_info_client();
  
  //in caz de ctrl+c se va trimite semnal
  signal(SIGINT, sigintHandler);

  /* crearea unui socket */
  if ((sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      perror ("[server]Eroare la socket().\n");
      return errno;
    }
  /* utilizarea optiunii SO_REUSEADDR */
  int on = 1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  
  /* pregatirea structurilor de date */
  bzero (&server, sizeof (server));
  bzero (&from, sizeof (from));
  
  /* umplem structura folosita de server */
  /* stabilirea familiei de socket-uri */
    server.sin_family = AF_INET;	
  /* acceptam orice adresa */
    server.sin_addr.s_addr = htonl (INADDR_ANY);
  /* utilizam un port utilizator */
    server.sin_port = htons (PORT);
  
  /* atasam socketul */
  if (bind (sd, (struct sockaddr *) &server, sizeof (struct sockaddr)) == -1)
    {
      perror ("[server]Eroare la bind().\n");
      return errno;
    }

  /* punem serverul sa asculte daca vin clienti sa se conecteze */
  if (listen (sd, 2) == -1)
    {
      perror ("[server]Eroare la listen().\n");
      return errno;
    }

  /* servim in mod concurent clientii...folosind thread-uri */
  while (1)
  {
    int client;
    thData * td; //parametru functia executata de thread     
    int length = sizeof (from);

    printf ("[server]Asteptam la portul %d...\n",PORT);
    fflush (stdout);

    // client= malloc(sizeof(int));
    /* acceptam un client (stare blocanta pina la realizarea conexiunii) */
    if ( (client = accept (sd, (struct sockaddr *) &from, &length)) < 0)
    {
      perror ("[server]Eroare la accept().\n");
      continue;
    }

    td = (struct thData*)malloc(sizeof(struct thData));	
    td->idThread = i++;
    td->cl = client;

    pthread_create(&th[i], NULL, &treat, td);	      
				
	}//while    
};				

static void *treat(void * arg)
{		
		struct thData tdL; 
		tdL= *((struct thData*)arg);	
		printf ("[thread]- %d - Asteptam mesajul...\n", tdL.idThread);
		fflush (stdout);		 
		pthread_detach(pthread_self());		
		raspunde((struct thData*)arg);
		/* am terminat cu acest client, inchidem conexiunea */
		close ((int*)arg);
		return(NULL);	
  		
};

void raspunde(void *arg)
{
  int login1 = 0, login2 = 0, stop = 0;
	struct thData tdL; 
  char buf[1000], parola[256], al_doilea_participant[256], cont[256], doriti_cont[256], nume[256], raspuns[1000];

  if(sqlite3_open_v2("baza_de_date.db", &baza_de_date, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, NULL) != SQLITE_OK)
  {
    printf("Eroare la deschiderea bazei de date !\n");
    return 1;
  }

  tdL = *((struct thData*)arg);

  memset(cont, 0, sizeof(cont));
  memset(doriti_cont, 0, sizeof(doriti_cont));
  memset(nume, 0, sizeof(nume));
  memset(parola, 0, sizeof(parola));
  memset(buf, 0, sizeof(buf));

  login1 = 0;
  login2 = 0;

  //pt "Aveti cont?"
  if (read (tdL.cl, &buf, sizeof(buf)) <= 0) // aici se intampla acea afisare continua
  {
      printf("[Thread %d]\n",tdL.idThread);
      perror ("Eroare la read() de la client.\n");		
  }
    
  printf ("1[Thread %d]Mesajul a fost receptionat...%s\n",tdL.idThread, buf);
  fflush (stdout);

  strcpy(cont, buf);
  printf("Aveti cont? : %s\n", cont);
  fflush (stdout);

  memset(buf, 0, sizeof(buf));

  if(strcmp(cont, "nu") == 0)
  {
    //pt "Doriti sa va faceti cont?"
    if(read (tdL.cl, &buf, sizeof(buf)) <= 0)
    {
      printf("[Thread %d]\n",tdL.idThread);
      perror ("Eroare la read() de la client.\n");		
    }
        
    printf ("2[Thread %d]Mesajul a fost receptionat...%s\n",tdL.idThread, buf);
    fflush (stdout);

    strcpy(doriti_cont, buf);
    printf("Doriti cont? : %s\n", doriti_cont);
    fflush (stdout);

    memset(buf, 0, sizeof(buf));
  }

  //daca nu are cont, dar vrea sa si creeze || are cont -> se merge mai departe
  if((strcmp(cont, "nu") == 0 && strcmp(doriti_cont, "da") == 0) || strcmp(cont, "da") == 0)
  {
    //pt nume
    if (read (tdL.cl, &buf, sizeof(buf)) <= 0)
    {
      printf("[Thread %d]\n",tdL.idThread);
      perror ("Eroare la read() de la client.\n");		
    }
      
    printf ("3[Thread %d]Mesajul a fost receptionat...%s\n",tdL.idThread, buf);
    fflush (stdout);

    strcpy(nume, buf);
    printf("Nume introdus : %s\n", nume);
    fflush (stdout);

    memset(buf, 0, sizeof(buf));

    //pt parola
    if (read (tdL.cl, &buf, sizeof(buf)) <= 0)
    {
      printf("[Thread %d]\n",tdL.idThread);
      perror ("Eroare la read() de la client.\n");		
    }
      
    printf ("4[Thread %d]Mesajul a fost receptionat...%s\n",tdL.idThread, buf);
    fflush (stdout);

    strcpy(parola, buf);
    printf("Parola introdusa : %s\n", parola);
    fflush (stdout);

    memset(buf, 0, sizeof(buf));
      
    if(strcmp(cont, "da") == 0)//are cont
    {
        //verificare corespondenta nume - parola - ok -> trec la alegere conv sau afisare utilizatori
        //                                       - nu e ok -> afisare mesaj nu e ok 
        //                                                       -> un while in care se repeta citirea parolei de 3 ori pana zice gata si se face stop = 1
        
        int ok = 0;
        sqlite3_stmt *stmt1;
        char comanda_verificare_user[256];

        snprintf(comanda_verificare_user, sizeof(comanda_verificare_user), "SELECT * FROM UTILIZATORI WHERE NUME = '%s' AND PAROLA = '%s'", nume, parola);

        if(sqlite3_prepare_v2(baza_de_date, comanda_verificare_user, -1, &stmt1, 0) != SQLITE_OK)
        {
          printf("Eroare la verificare utilizator : %s!\n", sqlite3_errmsg(baza_de_date));
          sqlite3_free(eroare);
          sqlite3_close(baza_de_date);
          return NULL;
        }

        int exista = 0;

        if(sqlite3_step(stmt1) == SQLITE_ROW)
        {
          exista = 1;
        }

        sqlite3_finalize(stmt1);

        if(!exista)
        {
          printf("Asociere incorecta!\n");

          //trimit raspuns la client daca asocierea e buna sau nu     
          strcpy(raspuns, "Asociere incorecta!");

          printf("[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
                    
          //returnam mesajul clientului 
          if (write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
          {
            printf("[Thread %d] (descriptor %d) ",tdL.idThread, tdL.cl);
            perror ("[Thread]Eroare la write() catre client.\n");
          }
          else
          {
            printf ("[Thread %d]Mesajul a fost transmis cu succes.\n",tdL.idThread);	
          }
        }
        else if(exista)
        {
          //activitatea utilizatorului devine 1 -> este activ
          strcpy(tdL.nume_client, nume);

          actualizare_info_client(nume, tdL.cl, tdL.idThread);

          char comanda_modificare_activitate[256];
            
          snprintf(comanda_modificare_activitate, sizeof(comanda_modificare_activitate), "UPDATE UTILIZATORI SET ACTIVITATE = 1 WHERE NUME = '%s'", tdL.nume_client);

          if(sqlite3_exec(baza_de_date, comanda_modificare_activitate, 0, 0, NULL) != SQLITE_OK)
          {
            printf("Eroare la modificare activitate: %s!\n", sqlite3_errmsg(baza_de_date));
            sqlite3_free(eroare);
            sqlite3_close(baza_de_date);
            return 2;
          }
            
          memset(comanda_modificare_activitate, 0, sizeof(comanda_modificare_activitate));

          ok = 1;
          login1 = 1;

          //trimit raspuns la client daca asocierea e buna sau nu   
          strcpy(raspuns, "Asociere corecta! Te ai logat!");

          printf("[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
                    
          //returnam mesajul clientului 
          if (write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
          {
            printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
            perror ("[Thread]Eroare la write() catre client.\n");
          }
          else
          {
            printf ("[Thread %d]Mesajul a fost transmis cu succes.\n",tdL.idThread);	
          }

          memset(raspuns, 0, sizeof(raspuns));
        }

        //exista numele utilizatorului care s a conectat in domeniul de valori al atributului 
        //OFFLINE in tabela MESAJE NECITITE ?
        //da -> afisez mesajele de acolo + le salvez in tabela MESAJE
        //        -> de aflat max(mesaj_id) de la conv care are conv_id
        //              in tabela MESAJE = cu cel din 'MESAJE NECITITE'
        //nu -> trec mai departe (nu e nevoie sa spun celui care s a conectat ca nu are mesaje necitite)

        //verific daca exista mesaje necitite
        sqlite3_stmt *stmt2;
        char comanda_verificare_mesaje_necitite[256];
        char mesaje_necitite[256];
        int nr_linie = 0, exista_mesaje_necitite = 0;

        memset(mesaje_necitite, 0, sizeof(mesaje_necitite));

        snprintf(comanda_verificare_mesaje_necitite, sizeof(comanda_verificare_mesaje_necitite), "SELECT ONLINE, MESAJ FROM \"MESAJE NECITITE\" WHERE OFFLINE = '%s'", tdL.nume_client);

        if(sqlite3_prepare_v2(baza_de_date, comanda_verificare_mesaje_necitite, -1, &stmt2, 0) != SQLITE_OK)
        {
          printf("Eroare la verificare mesaje necitite : %s!\n", sqlite3_errmsg(baza_de_date));
          sqlite3_free(eroare);
          sqlite3_close(baza_de_date);
          return 2;
        }

        if (sqlite3_step(stmt2) == SQLITE_ROW) 
        {
          strcat(mesaje_necitite, "MESAJE NECITITE :\n");
          exista_mesaje_necitite = 1;
        }

        sqlite3_reset(stmt2);

        while(sqlite3_step(stmt2) == SQLITE_ROW)
        {
          char *nume = sqlite3_column_text(stmt2, 0);
          char *mesaj = sqlite3_column_text(stmt2, 1); 

          snprintf(mesaje_necitite + strlen(mesaje_necitite), sizeof(mesaje_necitite) - strlen(mesaje_necitite), "%s : %s\n", nume, mesaj);
        }

        //printf("exista_mesaje_necitite : %d\n", exista_mesaje_necitite);
        printf("mesaje necitite : \n %s\n", mesaje_necitite);

        sqlite3_finalize(stmt2);
        memset(comanda_verificare_mesaje_necitite, 0, sizeof(comanda_verificare_mesaje_necitite));

        if(exista_mesaje_necitite)
        {
          //afisez mesajele necitite
          strcat(raspuns, mesaje_necitite);

          printf("[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
                    
          //returnam mesajul clientului 
          if (write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
          {
            printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
            perror ("[Thread]Eroare la write() catre client.\n");
          }
          else
          {
            printf ("[Thread %d]Mesajul a fost transmis cu succes.\n",tdL.idThread);	
          }  

          //trec mesajele din MESAJE NECITITE in MESAJE

          //exista o conv cu acelasi id din MESAJE NECITITE in MESAJE?
          // -> da - conv_id ramane acelasi
          //       - caut cel mai mare mesaj_id din MESAJE unde am conv_id ul respectiv
          // -> nu - caut max(conv_id) din MESAJE 
          //       - mesaj_id incepe de la 0
          
          sqlite3_stmt *stmt11;
          char comanda_aflare_conv_id1[256], comanda_aflare_conv_id2[256], online[256];
          int int_conv_id[100], i = 0;

          memset(online, 0, sizeof(online));

          for(i = 0; i < 100; i++)
          {
            int_conv_id[i] = -1;
          }
          i = 0;

          snprintf(comanda_aflare_conv_id1, sizeof(comanda_aflare_conv_id1), "SELECT DISTINCT CONV_ID, ONLINE FROM \"MESAJE NECITITE\" WHERE OFFLINE = '%s'", tdL.nume_client);

          if(sqlite3_prepare_v2(baza_de_date, comanda_aflare_conv_id1, -1, &stmt11, 0) != SQLITE_OK)
          {
            printf("Eroare la verificare mesaje necitite : %s!\n", sqlite3_errmsg(baza_de_date));
            sqlite3_free(eroare);
            sqlite3_close(baza_de_date);
            return 2;
          }

          while(sqlite3_step(stmt11) == SQLITE_ROW)
          {
            int_conv_id[i] = sqlite3_column_int(stmt11, 0);
            strcat(online, sqlite3_column_text(stmt11, 1));

            strcat(online, " ");
            i++;    
          }

          sqlite3_finalize(stmt11);
          memset(comanda_aflare_conv_id1, 0, sizeof(comanda_aflare_conv_id1));

          printf("int_conv_id :");
          for(i = 0; int_conv_id[i] != -1; i++)
          {
            printf(" %d", int_conv_id[i]);
          }
          printf("\n");
          printf("online : %s\n", online);

          i = 0;

          while(int_conv_id[i] != -1)
          { 
            //printf("int_conv_id[%d] : %d\n", i, int_conv_id[i]);

            int conv_id = -1, mesaj_id = 0; 
            char temp[256] = "", next[256];
            sqlite3_stmt* stmt12;

            conv_id = int_conv_id[i];

            sscanf(online, "%s", temp);
            //printf("temp : %s\n", temp);
            strcpy(online, online + strlen(temp) + 1);

            snprintf(comanda_aflare_conv_id2, sizeof(comanda_aflare_conv_id2), "SELECT CONV_ID FROM MESAJE WHERE (UTILIZATOR1 = '%s' AND UTILIZATOR2 = '%s') OR (UTILIZATOR1 = '%s' AND UTILIZATOR2 = '%s') LIMIT 1", tdL.nume_client, temp, temp, tdL.nume_client);

            if(sqlite3_prepare_v2(baza_de_date, comanda_aflare_conv_id2, -1, &stmt12, 0) != SQLITE_OK)
            {
              printf("Eroare la verificare utilizator : %s!\n", sqlite3_errmsg(baza_de_date));
              sqlite3_free(eroare);
              sqlite3_close(baza_de_date);
              return NULL;
            }

            //setare conv_id + mesaj_id

            if(sqlite3_step(stmt12) == SQLITE_ROW) //exista conv deja in mesaje intre cei doi
            {
              conv_id = sqlite3_column_int(stmt12, 0);
              mesaj_id = setare_mesaj_id(conv_id);
            }
            else //nu exista conv intre cei doi in mesaje
            {
              //aflam max(conv_id) din MESAJE

              char comanda_max_conv_id[256];
              sqlite3_stmt *stmt13;

              snprintf(comanda_max_conv_id, sizeof(comanda_max_conv_id), "SELECT MAX(CONV_ID) FROM MESAJE");

              if(sqlite3_prepare_v2(baza_de_date, comanda_max_conv_id, -1, &stmt13, 0) != SQLITE_OK)
              {
                printf("Eroare la verificare utilizator : %s!\n", sqlite3_errmsg(baza_de_date));
                sqlite3_free(eroare);
                sqlite3_close(baza_de_date);
                return NULL;
              }

              if(sqlite3_step(stmt13) == SQLITE_ROW)
              {
                conv_id = sqlite3_column_int(stmt13, 0) + 1;
                mesaj_id = setare_mesaj_id(conv_id);
              }

              sqlite3_finalize(stmt13);
              memset(comanda_max_conv_id, 0, sizeof(comanda_max_conv_id));
            }

            sqlite3_finalize(stmt12);
            memset(comanda_aflare_conv_id2, 0, sizeof(comanda_aflare_conv_id2));
            //am terminat de setat conv_id si mesaj_id

            //pregatim mesajele de transferat din MESAJE NECITITE in MESAJE
            //le stocam intr un sir de caractere

            char comanda_selectare_mesaje[256], mesaje_de_transferat[1000];
            sqlite3_stmt* stmt15;
            int j = 0;

            snprintf(comanda_selectare_mesaje, sizeof(comanda_selectare_mesaje), "SELECT MESAJ FROM \"MESAJE NECITITE\" WHERE OFFLINE = '%s' AND ONLINE = '%s'", tdL.nume_client, temp);

            if(sqlite3_prepare_v2(baza_de_date, comanda_selectare_mesaje, -1, &stmt15, 0) != SQLITE_OK)
            {
              printf("Eroare la verificare mesaje necitite : %s!\n", sqlite3_errmsg(baza_de_date));
              sqlite3_free(eroare);
              sqlite3_close(baza_de_date);
              return 2;
            }

            while(sqlite3_step(stmt15) == SQLITE_ROW)
            {
              strcat(mesaje_de_transferat, sqlite3_column_text(stmt15, 0));
              strcat(mesaje_de_transferat, "\n");
            }

            printf("mesajele de transferat : \n%s\n", mesaje_de_transferat);

            sqlite3_finalize(stmt11);
            memset(comanda_aflare_conv_id1, 0, sizeof(comanda_aflare_conv_id1));

            //transfer mesajele retinute din MESAJE NECITITE in MESAJE
            
            char comanda_transfer_mesaje[256], comanda_stergere_mesaje[256];
            char* mesaj;
            int k = 0;
            mesaj = strtok(mesaje_de_transferat, "\n");

            while(mesaj != NULL)
            {
              printf("mesajul de transferat in MESAJE: %s\n", mesaj);

              snprintf(comanda_transfer_mesaje, sizeof(comanda_transfer_mesaje), "INSERT INTO MESAJE (CONV_ID, UTILIZATOR1, UTILIZATOR2, MESAJ, MESAJ_ID) VALUES ('%d', '%s', '%s', '%s', '%d')", conv_id, temp, tdL.nume_client, mesaj, mesaj_id);

              if(sqlite3_exec(baza_de_date, comanda_transfer_mesaje, 0, 0, NULL) != SQLITE_OK)
              {
                printf("Eroare la transfer mesaj : %s!\n", sqlite3_errmsg(baza_de_date));
                sqlite3_free(eroare);
                sqlite3_close(baza_de_date);
                return 2;
              }

              printf("mesajul de sters din MESAJE NECITITE: %s\n", mesaj);

              snprintf(comanda_stergere_mesaje, sizeof(comanda_stergere_mesaje), "DELETE FROM \"MESAJE NECITITE\" WHERE ONLINE = '%s' AND OFFLINE = '%s' AND MESAJ = '%s'", temp, tdL.nume_client, mesaj);
              //printf("comanda: %s\n", comanda_stergere_mesaje);

              if(sqlite3_exec(baza_de_date, comanda_stergere_mesaje, 0, 0, NULL) != SQLITE_OK)
              {
                printf("Eroare la stergere mesaj : %s!\n", sqlite3_errmsg(baza_de_date));
                sqlite3_free(eroare);
                sqlite3_close(baza_de_date);
                return 2;
              }

              mesaj_id = setare_mesaj_id(conv_id);
              memset(comanda_transfer_mesaje, 0, sizeof(comanda_transfer_mesaje));
              mesaj = strtok(NULL, "\n");
            }
            
            memset(temp, 0, sizeof(temp));
            memset(mesaje_de_transferat, 0, sizeof(mesaje_de_transferat));
            i++;//ca sa treaca la urmatorul element din int_conv_id + urmatorul user de la care cel conectat are mesaje necitite
          }

          memset(online, 0, sizeof(online));
        }
        else if(!exista_mesaje_necitite)// nu sunt mesaje necitite
        {
          strcpy(raspuns, "Nu sunt mesaje necitite.\n");

          printf("[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
                    
          //returnam mesajul clientului 
          if (write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
          {
             printf("[Thread %d] (descriptor %d)",tdL.idThread, tdL.cl);
              perror ("[Thread]Eroare la write() catre client.\n");
          }
          else
          {
            printf ("[Thread %d]Mesajul a fost transmis cu succes.\n",tdL.idThread);	
          }  
        }

        memset(comanda_verificare_user, 0, sizeof(comanda_verificare_user));
        memset(raspuns, 0, sizeof(raspuns));
      }
      else if(strcmp(cont, "nu") == 0)//nu are cont
      {
        //creare cont -> adaugare in baza de date
      
        if(strcmp(doriti_cont, "nu") == 0)
        {
          //close la acel descriptor de la accept ? si eliminare din vectorul thData
          //nimic -> Bye
        }
        else if(strcmp(doriti_cont, "da") == 0)
        {
          int exista = 0;
          sqlite3_stmt *stmt3;
          char comanda_verificare_nume[256];
          
          snprintf(comanda_verificare_nume, sizeof(comanda_verificare_nume), "SELECT * FROM UTILIZATORI WHERE NUME = '%s'", nume);

          if(sqlite3_prepare_v2(baza_de_date, comanda_verificare_nume, -1, &stmt3, 0) != SQLITE_OK)
          {
            printf("Eroare la verificare utilizator : %s!\n", sqlite3_errmsg(baza_de_date));
            sqlite3_free(eroare);
            sqlite3_close(baza_de_date);
            return NULL;
          }

          if(sqlite3_step(stmt3) == SQLITE_ROW)
          {
            exista = 1;
          }
          else
          {
            exista = 0;
          }

          sqlite3_finalize(stmt3);

          if(!exista)
          {
            //numele nu se afla in baza de date -> numele e ok
            char comanda_adaugare_user[256];

            strcpy(tdL.nume_client, nume);

            actualizare_info_client(nume, tdL.cl, tdL.idThread);
            
            login2 = 1;
              
            snprintf(comanda_adaugare_user, sizeof(comanda_adaugare_user), "INSERT INTO UTILIZATORI (NUME, PAROLA, ACTIVITATE, DESCRIPTOR) VALUES ('%s', '%s', 1, '%d');", tdL.nume_client, parola, tdL.cl);
            printf("la adaugare user : %s\n", tdL.nume_client);
            if(sqlite3_exec(baza_de_date, comanda_adaugare_user, 0, 0, NULL) != SQLITE_OK)
            {
              printf("Eroare la adaugare utilizator : %s!\n", sqlite3_errmsg(baza_de_date));
              sqlite3_free(eroare);
              sqlite3_close(baza_de_date);
              return 2;
            }

            memset(comanda_adaugare_user, 0, sizeof(comanda_adaugare_user));

            //trimite mesaj clientului -> numele a fost adaugat in baza de date
            strcpy(raspuns, "Utilizator adaugat! Te ai logat!");

            //pregatim mesajul de raspuns      
            printf("[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
      
            //returnam mesajul clientului 
            if (write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
            {
              printf("[Thread %d] (descriptor %d)",tdL.idThread, tdL.cl);
              perror ("[Thread]Eroare la write() catre client.\n");
            }
            else
            {
              printf ("[Thread %d]Mesajul a fost transmis cu succes.\n",tdL.idThread);	
            }
          }
          else
          {
            //numele se afla deja in baza de date
            //int ok1 = 1;

            printf("Numele se afla deja in baza de date!\n");
              
            //trimite mesaj clientului -> numele se afla deja in baza de date 
            strcpy(raspuns, "Numele se afla deja in baza de date! Nu poti sa ti creezi cont cu acest nume!");

            //pregatim mesajul de raspuns      
            printf("[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
        
            //returnam mesajul clientului 
            if (write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
            {
              printf("[Thread %d] (descriptor %d)",tdL.idThread, tdL.cl);
              perror ("[Thread]Eroare la write() catre client.\n");
            }
            else
            {
              printf ("---[Thread %d]Mesajul a fost transmis cu succes.\n",tdL.idThread);	
            }

            memset(raspuns, 0, sizeof(raspuns));
          }
        }
      }

      afisare_info_client();
      verificare_activitate_clienti();
      afisare_info_client();

      if(login1 == 1 || login2 == 1)
      {
        //alegere al doilea participant
        if(read (tdL.cl, &buf, sizeof(buf)) <= 0)
        {
          printf("[Thread %d]\n",tdL.idThread);
          perror ("Eroare la read() de la client.\n");		
        }

        printf ("5[Thread %d]Mesajul a fost receptionat...%s\n",tdL.idThread, buf);
        fflush (stdout);

        strcpy(al_doilea_participant, buf);

        printf("Al doilea participant : %s\n", al_doilea_participant);
        fflush (stdout);

        memset(buf, 0, sizeof(buf));

        //verificari pt al doilea participant--------------------------------

        //exista acest participant in baza de date ?
        int exista = 0;
        sqlite3_stmt *stmt4;
        char comanda_verificare_nume[256];
            
        snprintf(comanda_verificare_nume, sizeof(comanda_verificare_nume), "SELECT * FROM UTILIZATORI WHERE NUME = '%s'", al_doilea_participant);
        
        if(sqlite3_prepare_v2(baza_de_date, comanda_verificare_nume, -1, &stmt4, 0) != SQLITE_OK)
        {
          printf("Eroare la verificare utilizator : %s!\n", sqlite3_errmsg(baza_de_date));
          sqlite3_free(eroare);
          sqlite3_close(baza_de_date);
          return NULL;
        }

        if(sqlite3_step(stmt4) == SQLITE_ROW)
        {
          exista = 1;
        }
        else
        {
          exista = 0;
        }

        sqlite3_finalize(stmt4);
        memset(comanda_verificare_nume, 0, sizeof(comanda_verificare_nume));

        if(!exista)
        {
          printf("Acest participant nu exista in baza de date.\n");

          actualizare_info_client_2(tdL.nume_client);

          //trimit mesaj catre client
          strcpy(raspuns, "Acest participant nu exista in baza de date.");

          //pregatim mesajul de raspuns      
          printf("---[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
      
          //returnam mesajul clientului 
          if(write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
          {
            printf("[Thread %d] (descriptor %d)",tdL.idThread, tdL.cl);
            perror ("[Thread]Eroare la write() catre client.\n");
          }

          close(tdL.cl);
          memset(raspuns, 0, sizeof(raspuns));
        }
        else
        {
          //participantul exista

          //trimit mesaj catre client ca al doilea participant e ok
          //de fapt e f posibil sa nu fie nevoie sa ii trimit si clientului informatia asta
          //cred ca doar daca e activ sau nu e necesar sa i spun
          strcpy(raspuns, "Acest participant exista in baza de data.");

          //pregatim mesajul de raspuns      
          printf("---[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
      
          //returnam mesajul clientului 
          if(write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
          {
            printf("[Thread %d] (descriptor %d)",tdL.idThread, tdL.cl);
            perror ("[Thread]Eroare la write() catre client.\n");
          }

          memset(raspuns, 0, sizeof(raspuns));

          //verificare activitate al doilea participant
          int activitate;

          activitate = verificare_activitate(al_doilea_participant);

          //se trimite mesaj clientului cu informatia despre activitatea persoanei cu care 
          //vrea sa vorbeasca

          if(activitate == 1)//al doilea participant e activ 
          {
            //trimit mesaj catre client cum ca al doilea participant e activ
            strcpy(raspuns, "Acest participant este activ.");

            //pregatim mesajul de raspuns      
            printf("[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
        
            //returnam mesajul clientului 
            if(write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
            {
              printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
              perror ("[Thread]Eroare la write() catre client.\n");
            }

            memset(raspuns, 0, sizeof(raspuns));

            //mai exista mesaje in MESAJE intre cei doi utilizatori?
            //da -> aflu max mesaj_id cu valoarea cea mai mare && id_conv il gasesc in tabela
            //nu -> aflu max conv_id && id_mesaj gasesc in tabela -> am functie pt setarea lui

            //pt ambele verificari de mai jos se cauta atat maria - ana cat si ana - maria

            //verificare daca mai exista mesaje intre cei doi useri pt a afla cel mai 
            //mare mesaj_id -> se continua de acolo

            //verificare daca mai exista mesaje intre cei doi useri pt a afla id_conv
            //daca nu exista se cauta conv_id cel mai mare din tabela mesaje si se da ++
            //pt a pune conv_id la discutia curenta 

            sqlite3_stmt *stmt6;
            int conv_id = 0, mesaj_id = 0, exista = 0;;
            char comanda_verificare_existenta_mesaj[256];

            snprintf(comanda_verificare_existenta_mesaj, sizeof(comanda_verificare_existenta_mesaj), "SELECT CONV_ID FROM MESAJE WHERE (UTILIZATOR1 = '%s' AND UTILIZATOR2 = '%s') OR (UTILIZATOR1 = '%s' AND UTILIZATOR2 = '%s') LIMIT 1", tdL.nume_client, al_doilea_participant, al_doilea_participant, tdL.nume_client);

            if(sqlite3_prepare_v2(baza_de_date, comanda_verificare_existenta_mesaj, -1, &stmt6, 0) != SQLITE_OK)
            {
              printf("Eroare la verificare existenta mesaj : %s!\n", sqlite3_errmsg(baza_de_date));
              sqlite3_free(eroare);
              sqlite3_close(baza_de_date);
              return NULL;
            }

            if(sqlite3_step(stmt6) == SQLITE_ROW)
            {
              exista = 1;
              conv_id = sqlite3_column_int(stmt6, 0);
            }

            sqlite3_finalize(stmt6);

            if(!exista) //nu mai exista mesaje inregistrare intre cei doi
            {
              mesaj_id = 0;

              //caut max(conv_id) din tabela mesaje
              char comanda_conv_id_max[256];
              sqlite3_stmt *stmt7;

              snprintf(comanda_conv_id_max, sizeof(comanda_conv_id_max), "SELECT MAX(CONV_ID) FROM MESAJE");

              if(sqlite3_prepare_v2(baza_de_date, comanda_conv_id_max, -1, &stmt7, 0) != SQLITE_OK)
              {
                printf("Eroare la aflare conv_id max : %s!\n", sqlite3_errmsg(baza_de_date));
                sqlite3_free(eroare);
                sqlite3_close(baza_de_date);
                return NULL;
              }

              if(sqlite3_step(stmt7) == SQLITE_ROW)
              {
                conv_id = sqlite3_column_int(stmt7, 0) + 1;
              }

              sqlite3_finalize(stmt7);
              memset(comanda_conv_id_max, 0, sizeof(comanda_conv_id_max));
            }
            else if(exista) //exista mesaje inregistrate intre cei doi
            {
              //conv_id e setat mai sus -> exista pt ca sunt deja mesaje inregistrate intre cei doi utilizatori

              mesaj_id = setare_mesaj_id(conv_id);
            }

            int trimite_mesaj = 1; 
            memset(comanda_verificare_existenta_mesaj, 0, sizeof(comanda_verificare_existenta_mesaj));

            afisare_info_client();

            int destinatie, reply_folosit = 0;

            destinatie = setare_destinatie(al_doilea_participant);

            if(destinatie == -1)
            {
              printf("***[Thread %d]Destinatia pentru %s nu a putut fi gasita!\n", tdL.idThread, al_doilea_participant);
              trimite_mesaj = 0;
            }
            else
            {
              printf("***[Thread %d]Destinatia pentru %s a putut fi gasita : %d\n", tdL.idThread, al_doilea_participant, destinatie);
            }
            
            while(trimite_mesaj)
            {
              printf("trimite_mesaj = %d\n", trimite_mesaj);

              //ar trebui sa verific de fiecare data daca al doilea participant e activ?
              //poate el se deconecteaza in timp ce client1 vb cu el
              //daca se intampla asta mesajele trimite de client1 trebuiesc puse in MESAJE NECITITE

              char mesaj[256], comanda_adaugare_mesaj[256];

              //se citeste mesajul
              if(read (tdL.cl, &buf, sizeof(buf)) <= 0)
              {
                printf("[Thread %d]\n",tdL.idThread);
                perror ("Eroare la read() de la client.\n");		
              }
              
              printf ("[Thread %d]Mesajul a fost receptionat...%s\n",tdL.idThread, buf);
              fflush (stdout);

              strcpy(mesaj, buf); 

              //pt comanda "istoric"
              if(strcmp(mesaj, "istoric") == 0)
              {
                printf("Utilizatorul a cerut istoricul mesajelor\n");

                //prelucrare -> stocare istoric intr un sir de caractere
                char comanda_aflare_istoric[256], istoric[1000];
                sqlite3_stmt *stmt16;

                snprintf(comanda_aflare_istoric, sizeof(comanda_aflare_istoric), "SELECT UTILIZATOR1, MESAJ, MESAJ_ID FROM MESAJE WHERE (UTILIZATOR1 = '%s' AND UTILIZATOR2 = '%s') OR (UTILIZATOR1 = '%s' AND UTILIZATOR2 = '%s')", al_doilea_participant, tdL.nume_client, tdL.nume_client, al_doilea_participant);

                if(sqlite3_prepare_v2(baza_de_date, comanda_aflare_istoric, -1, &stmt16, 0) != SQLITE_OK)
                {
                  printf("Eroare la aflare istoric mesaje : %s!\n", sqlite3_errmsg(baza_de_date));
                  sqlite3_free(eroare);
                  sqlite3_close(baza_de_date);
                  return NULL;
                }

                while(sqlite3_step(stmt16) == SQLITE_ROW)
                {
                  char *user = sqlite3_column_text(stmt16, 0);
                  char *mesaj2 = sqlite3_column_text(stmt16, 1); 
                  char *id_mesaj = sqlite3_column_int(stmt16, 2);

                  snprintf(istoric + strlen(istoric), sizeof(istoric) - strlen(istoric), "%s : %s - %d\n", user, mesaj2, id_mesaj);
                }

                strcpy(raspuns, istoric);

                //pregatim mesajul de raspuns      
                printf("[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
            
                //returnam mesajul clientului 
                if(write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
                {
                  printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
                  perror ("[Thread]Eroare la write() catre client.\n");
                }

                sqlite3_finalize(stmt16);
                memset(istoric, 0, sizeof(istoric));
                memset(raspuns, 0, sizeof(raspuns));
                memset(comanda_aflare_istoric, 0, sizeof(comanda_aflare_istoric));
              }

              if(strncmp(mesaj , "reply - ", 8) == 0)
              {
                //memset(mesaj, 0, sizeof(mesaj));

                int nr = 0;
                char *inceput_nr = mesaj + 8;
                char *inceput_mesaj = mesaj + 12; // partea de dupa |_ din comanda
                char comanda_mesaj_pt_reply[256], mesaj_final[256];
                sqlite3_stmt *stmt17;

                nr = atoi(inceput_nr);

                //aflam mesajul asupra caruia se aplica "reply"
                snprintf(comanda_mesaj_pt_reply, sizeof(comanda_mesaj_pt_reply), "SELECT MESAJ FROM MESAJE WHERE CONV_ID = '%d' AND MESAJ_ID = '%d'", conv_id, nr);

                if(sqlite3_prepare_v2(baza_de_date, comanda_mesaj_pt_reply, -1, &stmt17, 0) != SQLITE_OK)
                {
                  printf("Eroare la verificare existenta conversatie : %s!\n", sqlite3_errmsg(baza_de_date));
                  sqlite3_free(eroare);
                  sqlite3_close(baza_de_date);
                  return NULL;
                }

                if(sqlite3_step(stmt17) == SQLITE_ROW)
                {
                  strcat(mesaj_final, sqlite3_column_text(stmt17, 0));
                  strcat(mesaj_final, " | ");
                }

                //decupam mesajul trimis de utilizator
                strcat(mesaj_final, inceput_mesaj);

                //mesajul final va fi trimis catre al doilea participant
                printf("mesaj final : %s\n", mesaj_final);

                //mesaj o sa fie prelucrat ca sa fie retinut doar mesajul propriuzis, fara comanda reply 
                //reply - 5 | esti sigur -> mesaj o sa contina doar "esti sigur"

                strcpy(mesaj, inceput_mesaj); // cel care va fi stocat in tabela MESAJE
                strcpy(raspuns, mesaj_final); //cel care va fi afisat pe ecranul celui de al doilea participant

                //trimitem clientului numele celui care ii trimite mesajul
                if(write(destinatie, &tdL.nume_client, sizeof(tdL.nume_client)) <= 0)
                {
                  printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
                  perror ("[Thread]Eroare la write() catre client.\n");
                }

                //pregatim mesajul de raspuns      
                printf("[Thread %d]Trimitem mesajul la al doilea participant...%s\n",tdL.idThread, raspuns);
            
                //returnam mesajul clientului 
                if(write (destinatie, &raspuns, sizeof(raspuns)) <= 0)
                {
                  printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
                  perror ("[Thread]Eroare la write() catre client.\n");
                }

                reply_folosit = 1;
                
                sqlite3_finalize(stmt17);                
                memset(raspuns, 0, sizeof(raspuns));
                memset(mesaj_final, 0, sizeof(mesaj_final));
                memset(comanda_mesaj_pt_reply, 0, sizeof(comanda_mesaj_pt_reply));
              }

              if(strcmp(mesaj, "exit1") == 0)
              {
                trimite_mesaj = 0;

                //pregatim mesajul de raspuns      
                printf("[Thread %d]Trimitem mesajul la al doilea participant...%s\n",tdL.idThread, raspuns);
              
                //returnam mesajul clientului 
                if(write (destinatie, &mesaj, sizeof(mesaj)) <= 0)
                {
                  printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
                  perror ("[Thread]Eroare la write() catre client.\n");
                }              

                //activitatea utilizatorului devine 0
                actualizare_info_client_2(nume);

                /*
                char comanda_modificare_activitate[256];
                sqlite3_stmt *stmt19;
                  
                //snprintf(comanda_modificare_activitate, sizeof(comanda_modificare_activitate), "UPDATE UTILIZATORI SET ACTIVITATE = 0 WHERE NUME = '%s'", tdL.nume_client);
                snprintf(comanda_modificare_activitate, sizeof(comanda_modificare_activitate), "UPDATE UTILIZATORI SET ACTIVITATE = 0 WHERE NUME = ?");

                
                if(sqlite3_exec(baza_de_date, comanda_modificare_activitate, 0, 0, NULL) != SQLITE_OK)
                {
                  printf("Eroare la modificare activitate 1 -> 0 : %s!\n", sqlite3_errmsg(baza_de_date));
                  sqlite3_free(eroare);
                  sqlite3_close(baza_de_date);
                  return 2;
                }
                
                printf("nume folosit : %s\n", tdL.nume_client);
                if(sqlite3_prepare_v2(baza_de_date, comanda_modificare_activitate, -1, &stmt19, 0) != SQLITE_OK)
                {
                  printf("Eroare la modificare activitate 1 -> 0 : %s!\n", sqlite3_errmsg(baza_de_date));
                  sqlite3_free(eroare);
                  sqlite3_close(baza_de_date);
                  return 2;
                }

                printf("nume folosit : %s\n", tdL.nume_client);
                sqlite3_bind_text(stmt19, 1, tdL.nume_client, -1, SQLITE_STATIC);

                sqlite3_finalize(stmt19);
                memset(comanda_modificare_activitate, 0, sizeof(comanda_modificare_activitate));
                */

                //trimite_mesaj = 0;
                close(tdL.cl);
                printf("Utilizatorul a iesit din aplicatie.\n");
              }

              strcpy(raspuns, mesaj);
              if(strcmp(mesaj, "exit1") != 0)
              {
                printf("-----\nmesajul de adaugat in baza de date\n-----\n");
              }

              if(strcmp(mesaj, "exit1") != 0 && strcmp(mesaj, "istoric") != 0)
              {
                //trimitere catre al doilea participant 

                if(!reply_folosit)
                {
                  //trimitem clientului numele celui care ii trimite mesaj
                  if(write(destinatie, &tdL.nume_client, sizeof(tdL.nume_client)) <= 0)
                  {
                    printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
                    perror ("[Thread]Eroare la write() catre client.\n");   
                  }

                  //pregatim mesajul de raspuns      
                  printf("[Thread %d]Trimitem mesajul la al doilea participant...%s\n",tdL.idThread, raspuns);
              
                  //returnam mesajul clientului 
                  if(write (destinatie, &raspuns, sizeof(raspuns)) <= 0)
                  {
                    printf("err 1 [Thread %d] ",tdL.idThread);
                    perror ("[Thread]Eroare la write() catre client.\n");
                  } 
                }

                //salvare mesaj in tabela MESAJE
                char comanda_adaugare_mesaj[256];

                mesaj_id = setare_mesaj_id(conv_id);
                
                snprintf(comanda_adaugare_mesaj, sizeof(comanda_adaugare_mesaj), "INSERT INTO MESAJE (CONV_ID , UTILIZATOR1, UTILIZATOR2, MESAJ, MESAJ_ID) VALUES ('%d', '%s', '%s', '%s', '%d');", conv_id, tdL.nume_client, al_doilea_participant, mesaj, mesaj_id);

                if(sqlite3_exec(baza_de_date, comanda_adaugare_mesaj, 0, 0, NULL) != SQLITE_OK)
                {
                  printf("Eroare la adaugare mesaj : %s!\n", sqlite3_errmsg(baza_de_date));
                  sqlite3_free(eroare);
                  sqlite3_close(baza_de_date);
                  return 2;
                }

                memset(raspuns, 0, sizeof(raspuns));
                memset(comanda_adaugare_mesaj, 0, sizeof(comanda_adaugare_mesaj));
                memset(mesaj, 0, sizeof(mesaj));
                memset(buf, 0, sizeof(buf));
              }

              reply_folosit = 0;

              verificare_activitate_clienti();
            }
          }
          else if(activitate == 0)//al 2 lea participant nu e activ 
          {
            //MESAJELE SUNT SALVATE IN MESAJE NECITITE

            //trimit mesaj clientului ca al doilea participant nu e activ
            strcpy(raspuns, "Acest participant este inactiv.");

            //pregatim mesajul de raspuns
            printf("---[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
        
            //returnam mesajul clientului
            if(write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
            {
              printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
              perror ("[Thread]Eroare la write() catre client.\n");
            }

            memset(raspuns, 0, sizeof(raspuns));

            int trimite_mesaj = 1;
            char mesaj[256];

            //aflare conv_id
            sqlite3_stmt *stmt9;
            int conv_id = 0;
            char comanda_verificare_existenta_conv[256];

            snprintf(comanda_verificare_existenta_conv, sizeof(comanda_verificare_existenta_conv), "SELECT CONV_ID FROM MESAJE WHERE (UTILIZATOR1 = '%s' AND UTILIZATOR2 = '%s') OR (UTILIZATOR1 = '%s' AND UTILIZATOR2 = '%s') LIMIT 1", tdL.nume_client, al_doilea_participant, al_doilea_participant, tdL.nume_client);

            if(sqlite3_prepare_v2(baza_de_date, comanda_verificare_existenta_conv, -1, &stmt9, 0) != SQLITE_OK)
            {
              printf("Eroare la verificare existenta conversatie : %s!\n", sqlite3_errmsg(baza_de_date));
              sqlite3_free(eroare);
              sqlite3_close(baza_de_date);
              return NULL;
            }

            int exista = 0;

            if(sqlite3_step(stmt9) == SQLITE_ROW)
            {
              exista = 1;
              conv_id = sqlite3_column_int(stmt9, 0);
            }

            sqlite3_finalize(stmt9);
            memset(comanda_verificare_existenta_conv, 0, sizeof(comanda_verificare_existenta_conv));

            if(!exista) //nu mai exista mesaje inregistrare intre cei doi
            {
              //caut max(conv_id) din tabelele mesaje SI mesaje necitite
              // o sa il folosesc pe ala care e mai mare
              char comanda_conv_id_max_mesaje[256];
              int conv_id_1 = 0, conv_id_2 = 0;
              sqlite3_stmt *stmt10;

              snprintf(comanda_conv_id_max_mesaje, sizeof(comanda_conv_id_max_mesaje), "SELECT MAX(CONV_ID) FROM MESAJE");

              if(sqlite3_prepare_v2(baza_de_date, comanda_conv_id_max_mesaje, -1, &stmt10, 0) != SQLITE_OK)
              {
                printf("Eroare la aflare max(conv_id) din MESAJE : %s!\n", sqlite3_errmsg(baza_de_date));
                sqlite3_free(eroare);
                sqlite3_close(baza_de_date);
                return NULL;
              }

              if(sqlite3_step(stmt10) == SQLITE_ROW)
              {
                conv_id_1 = sqlite3_column_int(stmt10, 0) + 1;
              }

              sqlite3_finalize(stmt10);
              memset(comanda_conv_id_max_mesaje, 0, sizeof(comanda_conv_id_max_mesaje));

              char comanda_conv_id_max_mesaje_necitite[256];
              sqlite3_stmt *stmt14;

              snprintf(comanda_conv_id_max_mesaje_necitite, sizeof(comanda_conv_id_max_mesaje_necitite), "SELECT MAX(CONV_ID) FROM \"MESAJE NECITITE\"");

              if(sqlite3_prepare_v2(baza_de_date, comanda_conv_id_max_mesaje_necitite, -1, &stmt14, 0) != SQLITE_OK)
              {
                printf("Eroare la aflare max(conv_id) din MESAJE NECITITE : %s!\n", sqlite3_errmsg(baza_de_date));
                sqlite3_free(eroare);
                sqlite3_close(baza_de_date);
                return NULL;
              }

              if(sqlite3_step(stmt14) == SQLITE_ROW)
              {
                conv_id_2 = sqlite3_column_int(stmt14, 0) + 1;
              }

              sqlite3_finalize(stmt14);
              memset(comanda_conv_id_max_mesaje_necitite, 0, sizeof(comanda_conv_id_max_mesaje_necitite));

              if(conv_id_1 >= conv_id_2) conv_id = conv_id_1;
              else conv_id = conv_id_2;
            }

            while(trimite_mesaj)
            {
              char mesaj[256], comanda_adaugare_mesaj[256];

              //se citeste mesajul
              if(read (tdL.cl, &buf, sizeof(buf)) <= 0)
              {
                printf("[Thread %d]\n",tdL.idThread);
                perror ("Eroare la read() de la client.\n");
                trimite_mesaj = 0; // pune asta aici pt atunci cand da clientul Ctrl + C
              }
          
              printf ("!!6[Thread %d]Mesajul a fost receptionat...%s\n",tdL.idThread, buf);
              fflush (stdout);

              strcpy(mesaj, buf);  

              //pt comanda "istoric"
              if(strcmp(mesaj, "istoric") == 0)
              {
                printf("Utilizatorul a cerut istoricul mesajelor\n");

                //prelucrare -> stocare istoric intr un sir de caractere
                char comanda_aflare_istoric[256], istoric[1000];
                sqlite3_stmt *stmt16;

                snprintf(comanda_aflare_istoric, sizeof(comanda_aflare_istoric), "SELECT UTILIZATOR1, MESAJ, MESAJ_ID FROM MESAJE WHERE (UTILIZATOR1 = '%s' AND UTILIZATOR2 = '%s') OR (UTILIZATOR1 = '%s' AND UTILIZATOR2 = '%s')", al_doilea_participant, tdL.nume_client, tdL.nume_client, al_doilea_participant);

                if(sqlite3_prepare_v2(baza_de_date, comanda_aflare_istoric, -1, &stmt16, 0) != SQLITE_OK)
                {
                  printf("Eroare la aflare istoric mesaje : %s!\n", sqlite3_errmsg(baza_de_date));
                  sqlite3_free(eroare);
                  sqlite3_close(baza_de_date);
                  return NULL;
                }

                while(sqlite3_step(stmt16) == SQLITE_ROW)
                {
                  char *user = sqlite3_column_text(stmt16, 0);
                  char *mesaj2 = sqlite3_column_text(stmt16, 1); 
                  char *id_mesaj = sqlite3_column_int(stmt16, 2);

                  snprintf(istoric + strlen(istoric), sizeof(istoric) - strlen(istoric), "%s : %s - %d\n", user, mesaj2, id_mesaj);
                }

                strcpy(raspuns, istoric);

                //pregatim mesajul de raspuns      
                printf("[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
            
                //returnam mesajul clientului 
                if(write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
                {
                  printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
                  perror ("[Thread]Eroare la write() catre client.\n");
                }

                sqlite3_finalize(stmt16);
                memset(mesaj, 0, sizeof(mesaj));
                memset(istoric, 0, sizeof(istoric));
                memset(raspuns, 0, sizeof(raspuns));
                memset(comanda_aflare_istoric, 0, sizeof(comanda_aflare_istoric));
              }

              if(strncmp(mesaj , "reply - ", 8) == 0)
              {
                int nr = 0;
                char *inceput_nr = mesaj + 8;
                char *inceput_mesaj = mesaj + 12;
                char comanda_mesaj_pt_reply[256], mesaj_final[256];
                sqlite3_stmt *stmt17;

                nr = atoi(inceput_nr);
                strcat(mesaj_final, inceput_mesaj);

                //decupam mesajul utilizatorului

                //aflam mesajul asupra caruia se aplica "reply"
                snprintf(comanda_mesaj_pt_reply, sizeof(comanda_mesaj_pt_reply), "SELECT MESAJ FROM MESAJE WHERE CONV_ID = '%d' AND MESAJ_ID = '%d'", conv_id, nr);

                if(sqlite3_prepare_v2(baza_de_date, comanda_mesaj_pt_reply, -1, &stmt17, 0) != SQLITE_OK)
                {
                  printf("Eroare la verificare existenta conversatie : %s!\n", sqlite3_errmsg(baza_de_date));
                  sqlite3_free(eroare);
                  sqlite3_close(baza_de_date);
                  return NULL;
                }

                if(sqlite3_step(stmt17) == SQLITE_ROW)
                {
                  strcat(mesaj_final, sqlite3_column_text(stmt17, 0));
                  strcat(mesaj_final, " | ");
                }

                strcat(mesaj_final, inceput_mesaj);

                //mesajul final va fi trimis catre al doilea participant
                printf("mesaj final : %s\n", mesaj_final);

                //mesaj o sa fie prelucrat ca sa fie retinut doar mesajul propriuz si, fara comanda reply 
                //reply - 5 | esti sigur -> mesaj o sa contina doar "esti sigur"

                strcpy(mesaj, inceput_mesaj);

                strcpy(raspuns, mesaj_final);

                //pregatim mesajul de raspuns      
                printf("[Thread %d]Trimitem mesajul inapoi...%s\n",tdL.idThread, raspuns);
            
                //returnam mesajul clientului 
                if(write (tdL.cl, &raspuns, sizeof(raspuns)) <= 0)
                {
                  printf("[Thread %d] (descriptor: %d)",tdL.idThread, tdL.cl);
                  perror ("[Thread]Eroare la write() catre client.\n");
                }
                
                sqlite3_finalize(stmt17);       
                memset(mesaj, 0, sizeof(mesaj));         
                memset(raspuns, 0, sizeof(raspuns));
                memset(mesaj_final, 0, sizeof(mesaj_final));
                memset(comanda_mesaj_pt_reply, 0, sizeof(comanda_mesaj_pt_reply));
              }

              //user ul a iesit din aplicatie -> ar trebui pana la final sa fie in loc de aplicatie -> conversatie
              if(strcmp(mesaj, "exit1") == 0)
              {
                actualizare_info_client_2(tdL.nume_client);

                //activitatea utilizatorului devine 0

                char comanda_modificare_activitate[256];
                  
                snprintf(comanda_modificare_activitate, sizeof(comanda_modificare_activitate), "UPDATE UTILIZATORI SET ACTIVITATE = 0 WHERE NUME = '%s'", tdL.nume_client);

                if(sqlite3_exec(baza_de_date, comanda_modificare_activitate, 0, 0, NULL) != SQLITE_OK)
                {
                  printf("Eroare la modificare activitate 1 -> 0 : %s!\n", sqlite3_errmsg(baza_de_date));
                  if(eroare != NULL)
                  {
                    printf("aici la eroare\n");
                    sqlite3_free(eroare);
                  }
                  sqlite3_free(NULL);
                  sqlite3_close(baza_de_date);
                  return 2;
                }

                memset(comanda_modificare_activitate, 0, sizeof(comanda_modificare_activitate));

                trimite_mesaj = 0;

                close(tdL.cl);
                printf("Utilizatorul a iesit din aplicatie.\n");
              }

              //salvare mesaj in tabela MESAJE NECITITE
              if(strcmp(mesaj, "exit1") != 0 && strcmp(mesaj, "istoric") != 0)
              {
                char comanda_adaugare_mesaj[256];
                
                snprintf(comanda_adaugare_mesaj, sizeof(comanda_adaugare_mesaj), "INSERT INTO \"MESAJE NECITITE\" (CONV_ID , ONLINE, OFFLINE, MESAJ) VALUES ('%d', '%s', '%s', '%s');", conv_id, tdL.nume_client, al_doilea_participant, mesaj);

                if(sqlite3_exec(baza_de_date, comanda_adaugare_mesaj, 0, 0, NULL) != SQLITE_OK)
                {
                  printf("Eroare la adaugare mesaj : %s!\n", sqlite3_errmsg(baza_de_date));
                  sqlite3_free(eroare);
                  sqlite3_close(baza_de_date);
                  return 2;
                }

                memset(comanda_adaugare_mesaj, 0, sizeof(comanda_adaugare_mesaj));
                memset(mesaj, 0, sizeof(mesaj));
                memset(buf, 0, sizeof(buf));
              }

              verificare_activitate_clienti();
            }
          }
        }
      }

    verificare_activitate_clienti();
    afisare_info_client();;
  }

  sqlite3_close(baza_de_date);
}

//cand da ctrl + c din client, clientul trebuie sters din vectorul thData 
//      -> modificata activitatea din baza de date
//cand da ctrl + c din client serverul se inchide sau merge la infinit (daca se da din
//bucla while(trimite_mesaj)) -> serverul nu trebuie sa se opreasca si nici sa mearga la infint
//parola trebuie criptata -> hashing