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
#include <sys/wait.h>
#include <sys/types.h>

void sig_handler(int sig);
void *read_input(void *p);
void *exec_cmd(void *p);
void call_cmd(void);
void call_execvp(char *cmd, char *argv[]);

char *buffer;
volatile sig_atomic_t program_exit = 0; /* ukoncenie programu */
pthread_t thread_read,thread_exec;
pthread_cond_t cond;
pthread_mutex_t mutex;

void sig_handler(int sig){
    #ifdef DEBUG
        printf("\n\nsingal\n\n");
    #endif
    /* DOCASNE - IMPLENENTOVAT UKONCENIE PROCESU NA POZADI */
    program_exit = 1;
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

    if(fork() == 0) {
		execvp("clear", argv);
		exit(1);
	} else {
		wait(NULL);
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

void *read_input(void *p){

    int rlen;

    while(!program_exit){
        pthread_mutex_lock(&mutex);

        /* ak nie je buffer prazdny, tak cakaj */
        while(strlen(buffer) > 0)
                pthread_cond_wait(&cond,&mutex);

        /* zobrazi shell text */
        printf(SHELL_TEXT);
        fflush(stdout);

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

        call_cmd();

        /* vyprazdni buffer */
        memset(buffer, 0,BUFFER_SIZE);
        pthread_cond_signal(&cond);

        pthread_mutex_unlock(&mutex);


    }

    return (void *) 0;
}

void call_cmd(void){

    /* spracovavat prazdne prikazy nema vyznam */
    if(strlen(buffer) < 2) return;

    #ifdef DEBUG
        printf("prikaz:%s",buffer);
    #endif


    int i = 0;
    char *argv[100];
    char *ret_token;
    char *rest = "";

    /* prechadza cely retazec a vytvara pole */
    while((ret_token = strtok_r(buffer, " ", &rest)) != NULL){
        /* odstrani znak noveho riadku */
        if(ret_token[strlen(ret_token) - 1] == '\n'){
            ret_token[strlen(ret_token) - 1] = 0;
        }
        argv[i++] = ret_token;
        buffer = rest;
    }

    /* na poslednu poziciu NULL */
    argv[i] = NULL;

    #ifdef DEBUG
        int j;
        printf("\nDEBUG\n");
        for(j = 0; j < i; j++){
            printf("argv %d: %s\n",j,argv[j]);
        }
        printf("\n");
    #endif

    /* ak je prikaz cd */
    if(strcmp(argv[0],"cd") == 0){
        if(chdir(argv[1]) != 0){
            perror("shell: cd");
        }

        return;
    }

    /* kontorla ci parameter neobsahuje presmerovanie vystupu > */



    /* TREBA IMPLENTOVAT NECO ROZUMNEJSIE  */
    if(strcmp(argv[0],"exit") == 0){
        program_exit = 1;
        return;
    }

    /*
     * vykonnanie samostatneho prikazu bez presmerovania vstupu/vystupu
     * alebo spustenia na pozadi
     */
    call_execvp(argv[0],argv);
}

void call_execvp(char *cmd, char *argv[]){

    pid_t id;
    int status;

    if((id = fork()) == -1){
        printf("fork error\n");
        exit(EXIT_FAILURE);
    }

    /* child vykona prikaz a rodic pocka na jeho dokoncenie */
    if(id == 0){
        if(execvp(argv[0],argv) == -1){
            printf("shell: %s: command not found\n",argv[0]);
            exit(EXIT_FAILURE);
        }
    }else{
        if(waitpid(id,&status,0) == -1){
            printf("waitpid error\n");
            exit(EXIT_FAILURE);
        }
    }

}

