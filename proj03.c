#define _POSIX_C_SOURCE 199506L
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1

#define DEBUG
#define BUFFER_SIZE 512
#define SHELL_TEXT "shell> "
#undef getchar/*treba zistit ci je getchar threadsafe*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

char *buffer;
volatile sig_atomic_t program_exit = 0; /* ukoncenie programu */
pthread_t thread_read,thread_exec;
pthread_cond_t cond;
pthread_mutex_t mutex;

void sig_handler(int sig){

    /* DOCASNE - IMPLENENTOVAT UKONCENIE PROCESU NA POZADI */
    program_exit = 1;
}

void *read_input(void *p){

    int rlen;

    while(!program_exit){
        pthread_mutex_lock(&mutex);

        /* ak nie je buffer prazdny, tak cakaj */
        while(strlen(buffer) > 0)
                pthread_cond_wait(&cond,&mutex);

        /* nacitaj vstup a ak je prilis dlhy tak chyba */
        if((rlen = read(STDIN_FILENO,buffer,BUFFER_SIZE)) == -1){
            exit(EXIT_FAILURE);
        }

        /* ak je spravne velkost vstupu moze sa spracovat */
        if(rlen < BUFFER_SIZE){
            pthread_cond_signal(&cond);
        }else{
            printf("Error: Input is too long %d \n",rlen);
            /* vyprazdni stdin a buffer */
            while (getchar() != '\n');
            memset(buffer, 0, BUFFER_SIZE);
        }

        pthread_mutex_unlock(&mutex);
    }

    return (void *) 0;
}

void *exec_cmd(void *p){

    while(!program_exit){

        /* treba pockat na signal od input vlakna */
        pthread_mutex_lock(&mutex);

        /* ak je prazdny buffer tak cakaj */
        while(strlen(buffer) == 0)
            pthread_cond_wait(&cond,&mutex);

        printf("cmd:%s",buffer);

        /* vyprazdni buffer */
        memset(buffer, 0,BUFFER_SIZE);
        pthread_cond_signal(&cond);

        pthread_mutex_unlock(&mutex);
    }

    return (void *) 0;
}

int main(int argc, char* argv[], char **envp){

    struct sigaction sigact;
    pthread_attr_t attr;
    int res;

    if((buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE)) == NULL){
        printf("buffer mallock error\n");
        return 1;
    }

    sigact.sa_flags = 0;
    sigact.sa_handler = sig_handler;
    sigemptyset(&sigact.sa_mask);

    if(sigaction(SIGINT,&sigact,NULL)){
		printf("sigaction() error\n");
		return 1;
	}

	if((res = pthread_mutex_init(&mutex,NULL)) != 0){
        printf("pthread_mutex_init() error %d\n",res);
        return 1;
	}

    if((res = pthread_attr_init(&attr)) != 0){
        printf("pthread_attr_init() error %d\n",res);
        return 1;
    }

    if((res = pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE)) != 0){
        printf("pthread_attr_setdetachstate() error %d\n",res);
        return 1;
    }

    if((res = pthread_cond_init(&cond,NULL)) != 0){
        printf("pthread_cond_init() error %d\n",res);
        return 1;
    }

    /* vlakno cita vstup od uzivatela */
    if((res = pthread_create(&thread_read,&attr,read_input,NULL)) != 0){
        printf("pthread_create() error %d\n",res);
        return 1;
    }

    /* vlakno spracovava jednotlive prikazy */
    if((res = pthread_create(&thread_exec,&attr,exec_cmd,NULL)) != 0){
        printf("pthread_create() error %d\n",res);
        return 1;
    }

    /* atributy uz netreba */
    if((res = pthread_attr_destroy(&attr)) != 0){
        printf("pthread_attr_destroy() error %d\n",res);
        return 1;
    }

    if ((res = pthread_join(thread_read,NULL)) != 0){
        printf("pthread_join() error %d\n",res);
        return 1;
    }

    if ((res = pthread_join(thread_exec,NULL)) != 0){
        printf("pthread_join() error %d\n",res);
        return 1;
    }

	return 0;
}
