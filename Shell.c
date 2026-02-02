#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/types.h>

#define delimiter " " // next we will cover more delimiter "\n \t | ..."
#define  max_size 64


void execute_command(char **tokens){
    pid_t pid;
    int status;
    pid = fork();
    if(pid == -1 ){
        perror("error to create a child process \n");
    }
    else if (pid == 0)
    {
        if(execvp(tokens[0],tokens)==-1){
            perror("error detected");
        }
        exit(1);
    }
    else
    {
        wait(NULL);
    }
    
    
    
}

char** split_command(char* cmd){ // we split the command into arguments     
    char *delim=delimiter;
    int buffer_size=max_size;
    char** tokens = malloc(sizeof(char*)*buffer_size);
    char* token;
    int pos=0;
    if (!tokens){
        fprintf(stderr,"error allocation \n");
        exit(0);
    }
    token = strtok(cmd,delim);
    while (token != NULL)
    {
        tokens[pos]=token;
        pos++;
        token=strtok(NULL,delim);
    }
    tokens[pos]=NULL;
    return tokens;

}



char* write_command(){
    int buffer_size=max_size;
    char *cmd = (char*)malloc(sizeof(char)*buffer_size); // allocation of 1 bloc (we will add more space later)
    int c;
    int pos=0;
    if(!cmd){
        printf("error allocation \n");
        exit(0);
    }
    while (1)
    {
       
        c=getchar();
        if(c==EOF){
            free(cmd);
            printf("\nexiting yazid@Shell ... \n");
            exit(0);
        }
        if(c=='\n'){
            cmd[pos]='\0';
            return cmd;
        }
        else{
            cmd[pos]=(char)c;
        }
        pos++;

        if(pos>=buffer_size){
            buffer_size*=2;
            cmd=realloc(cmd,buffer_size);
            if(!cmd){
                fprintf(stderr,"allocation error \n");
                exit(0);
            }   

        }
    }   
    
}





void line_loop(){
    char *command;
    char **arg;
    do
    {
        printf("yazid-Shell @ > ");
        fflush(stdout);
        command =write_command();
        arg=split_command(command);
        execute_command(arg);
        free(arg);
        if (command && strlen(command)>0)
            printf("\ncommand:  %s recieved \n",command);
        free(command);
    } while (1);
}




int main( int argc, char **argv){
    line_loop();
    return 0 ;
}   
