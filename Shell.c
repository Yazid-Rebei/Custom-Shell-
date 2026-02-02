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
        perror("error \n");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        if(execvp(tokens[0],tokens)==-1){
            perror("error");
        }
        exit(EXIT_FAILURE);
    }
    else
    {
        wait(NULL);
    }
}

void cd_command(char** tokens){
    if (tokens !=NULL){
        if(chdir(tokens[1])!=0){
            perror("error ");
        }
        else{
            printf("change directory to %s\n",tokens[1]);
        }
    }
}

void help_command(char** tokens){
    if(tokens != NULL){
        printf("help menu   : \n 1.use man page : man command \n 2.use --help : command --help \n 3.use apropos : apropos keyword\n");
    }
}

void type_command(char** tokens){
    if(tokens != NULL){
            char *builtin_list[] ={"cd","help","type","sname",NULL};
            int found = 0;
            for(int i =0 ; builtin_list[i]!=NULL;i++){
                if (strcmp(tokens[1],builtin_list[i])==0){
                    printf("%s is built in command \n",tokens[1]); 
                    found = 1;
            }
        }
            if(found ==0){
                printf("%s is external command\n",tokens[1]);
            }
    }
}

void sname_command(char** tokens){
    if (tokens != NULL)
    {
        printf("Shell name : yazid Shell \n");
    }
    
}




void  check_command(char **tokens){
    char *builtin_list[] ={"cd","help","type","sname",NULL};
    if(tokens == NULL){
        exit(EXIT_FAILURE);
    }
    else{
        void (*builin_function[])(char**)={cd_command,help_command,type_command,sname_command};
        int found=0;
        for(int i =0 ;builtin_list[i] != NULL;i++){
            if(strcmp(tokens[0],builtin_list[i])==0){
                builin_function[i](tokens);
                found = 1;
                break;
            }
    
        }
        if (found == 0){
            execute_command(tokens);
        }
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
        check_command(arg);
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
