/* Name: David Lee
 * Date: 07/20/2020
 *
 */
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>


//return exit value or signal used for return value of "status" command
int exit_status = 0;

//background flag used by signal
int bg_enable = 1;

//flag to indicate whether to execvp "non-built" command
//used to avoid submitting user input when toggling background/foreground signal
int exec_run = 1;

//stores child status, parameter used to check how process was terminated
int child_status;

//count of background processes - used keep track of background processes
int bg_count = 0;

//fixed array to store background process ids. Used to kill all processes during exit
int bg_tracker[512];

//process position in bg tracker array
int tracker_pos = 0;

//killing all background running processes when exiting
void exit_process(){
    for(int i = 0; i < 512; i++){
        if(bg_tracker[i] != -5){
            kill(bg_tracker[i], SIGTERM);
            printf("Killing pid %d following exit command\n", bg_tracker[i]);
            fflush(stdout);
        }
    }
}

//function used to toggle background/foreground mode via sigtstp signal (ctrl + z)
void handle_sigtstp(int signo){
    if (bg_enable == 1) {
        char* message = "Entering foreground-only mode (& is now ignored)";
        printf("%s\n", message);
        fflush(stdout);
        bg_enable = 0; //background off
        exec_run = 0; //flag to indicate whether to execvp "non-built" command
    }
    else {
        char* message = "Exiting foreground-only mode";
        printf("%s\n", message);
        fflush(stdout);
        bg_enable = 1; //background on
        exec_run = 0; //flag to indicate whether to execvp "non-built" command
    }
}

//Reference used for replacing sub-string and expansion:
// https://www.geeksforgeeks.org/c-program-replace-word-text-another-given-word/
char * expand_pid(const char *s, const char * pid){
    //placeholder for return value
    char * result;
    int i = 0;
    //counts number of "$$" in a string
    int occur_ct = 0;

    //iterate within string
    for (i = 0; s[i] != '\0'; i++){
        if (strstr(&s[i], "$$") == &s[i]){
            occur_ct++;
            //Jumping to index after the "$$" in string.
            i += strlen("$$") - 1;
        }
    }
    //dynamically allocating size for placeholder
    result = (char *)malloc(i + occur_ct * (strlen(pid) - strlen("$$")) + 1);
    i = 0;
    while (*s){
        //compare the substring with the result
        if (strstr(s, "$$") == s){
            strcpy(&result[i], pid);
            i += strlen(pid);
            s += strlen("$$");
        }else{
            result[i++] = *s++;
        }
    }
    result[i] = '\0';
    return result;
}


//function checks for any child processes running in the background
//Reference used: "The Linux Programming Interface" by Michael Kerrisk (pg 557)
void check_bg(){
    pid_t childPid;
    //checks for finished background process
    while ((childPid = waitpid(-1, &child_status, WNOHANG)) > 0){
        bg_count--;
        //process ended normally
        if (WIFEXITED(child_status) != 0){
            printf("background pid %d is done: exit value %d\n", childPid, WEXITSTATUS(child_status));
            fflush(stdout);
            //process ended with signal
        }else if (WIFSIGNALED(child_status)){
            printf("background pid %d is done: terminated by signal %d\n", childPid, WTERMSIG(child_status));
            fflush(stdout);
        }
        //removes background process that ended normally from background process tracker
        for(int i = 0; i < 512; i++){
            if(childPid == bg_tracker[i]){
                bg_tracker[i] = -5;
                break;
            }
        }
    }
}

//functions checks if user input contains '&' as the last character to toggle background mode
int bg_char(char * input) {
    if(input[strlen(input)-2] == '&') {
        return 1;
    }else{
        return 0;
    }
}

int main() {

    //initialize value in background tracker
    for(int i = 0; i < 512; i++){
        //using dummy value of -5
        bg_tracker[i] = -5;
    }

    //CS344 Class Module "Exploration: Signal Handling API"
    //sigaction structure (signal from ctrl + c)
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);
    //sigaction structure (signal from ctrl + z)
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_sigtstp;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    //flag used when input contains '&' char to start background process
    int bg = 0;
    //flag used for looping user input in shell
    int shell = 1;

    //flag to indicate last foreground process was terminated by signal
    int last_signal_exit = 0;
    //user input with upto 512 arguments
    char * input[512];
    //input file name used in file descriptor
    char * input_file = "";
    //output file name used in file descriptor
    char * output_file = "";
    //stores fd input
    int fd_input;
    //stores fd output
    int fd_output;
    //stores new file descriptor for dup2() function
    int result;
    //user input buffer from user prompt
    char buffer[2048];
    //stores shell pid
    char shell_pid[6];
    //convert int to string
    sprintf(shell_pid, "%d", getpid());
    //delimiter used when parsing in strtok (user input arguments)
    const char delim[] = " \n";

    //loop shell prompt infinitely until user send "exit" command
    while(shell){
        //reaping background child processes

        //reset user input arguments
        for(int i = 0; i < 512; i++){
            input[i] = NULL;
        }
        //check for bg processes if background process count is greater than 0
        if(bg_count > 0){
            check_bg();
        }
        //display colon in console
        printf(":");
        fflush(stdout);
        //requesting user input
        fgets(buffer, sizeof(buffer)-1, stdin);

        //checks if user input is a comment or is empty
        if(buffer[0] == '#' || buffer[0] == '\n' || buffer[0] == '\0' || buffer[0] == ' '){ // empty command
            continue;
        }else{
            //checks if user input contains background mode command ('&')
            if(bg_char(expand_pid(buffer, shell_pid))){
                bg = 1;
            }
            //parsing user input into arguments recognized by the shell
            char *tok = strtok(expand_pid(buffer, shell_pid), delim);
            int i = 0;
            while (tok != NULL){
                //checks for input redirection
                if(strcmp(tok, "<") == 0){
                    tok = strtok(NULL, delim);
                    input_file = tok;
                //checks for output redirection
                }else if(strcmp(tok, ">") == 0) {
                    tok = strtok(NULL, delim);
                    output_file = tok;
                //checks for ampersand that indicates background process
                }else if((strcmp(tok, "&") == 0) && bg == 1){
                    tok = strtok(NULL, delim);
                }
                else{
                    //store user input as argument
                    input[i++] = strdup(tok);
                }
                tok = strtok(NULL, delim);
            }
            fflush(stdin);

            //built-in exit command to end shell program
            if(strcmp(input[0], "exit") == 0){
                exit_process();
                shell = 0;
            //built-in command to change working directory
            }else if(strcmp(input[0], "cd") == 0){
                if (input[1] != NULL) {
                    //checks if directory exists
                    if (chdir(input[1]) != 0){
                        printf("Directory not found.\n");
                        fflush(stdout);
                    }
                }else{
                    //return directory back to home
                    chdir(getenv("HOME"));
                    fflush(stdout);
                }
            //returns exit value / termination signal of last foreground process
            }else if (strcmp(input[0], "status") == 0){
                if (last_signal_exit){
                    printf("terminated by signal %d\n", exit_status);
                    fflush(stdout);
                }else{
                    printf("exit value %d\n", exit_status);
                    fflush(stdout);
                }
            }else{
                //forks child process to execute non-built-in commands
                if(exec_run){
                    pid_t spawnPid = -5;
                    spawnPid = fork(); // Fork a new process from running parent process
                    switch (spawnPid) {
                        case -1: //error in fork process
                            perror("fork()\n");
                            exit(1);
                            break;
                        case 0: //child process
                            //allow signal interrupt in child process (ctrl + c)
                            //back to default behavior of signal
                            SIGINT_action.sa_handler = SIG_DFL;
                            SIGINT_action.sa_flags = 0;
                            sigaction(SIGINT, &SIGINT_action, NULL);

                            //CS344 Class Module "Exploration: Processes and I/O"
                            //redirecting input file descriptor
                            if (strcmp(input_file, "") != 0){
                                //Open source file
                                fd_input = open(input_file, O_RDONLY);
                                if (fd_input == -1){
                                    printf("cannot open %s for input\n", input_file);
                                    fflush(stdout);
                                    exit(1);
                                }
                                //Redirect stdin to source file
                                result = dup2(fd_input, 0);
                                if (result == -1){
                                    perror("source dup2()");
                                    exit(2);
                                }
                                //Close File Descriptor On Exec
                                fcntl(fd_input, F_SETFD, FD_CLOEXEC);
                            }

                            //redirecting output file descriptor
                            if (strcmp(output_file, "") != 0){
                                //Open target file
                                fd_output = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                if (fd_output == -1){
                                    printf("cannot open %s for input\n", output_file);
                                    fflush(stdout);
                                    exit(1);
                                }
                                //Redirect stdout to target file
                                result = dup2(fd_output, 1);
                                if (result == -1) {
                                    perror("target dup2()");
                                    exit(2);
                                }
                                //Close File Descriptor On Exec
                                fcntl(fd_output, F_SETFD, FD_CLOEXEC);
                            }
                            //background mode only for empty fd_input and fd_output
                            if(bg && bg_enable){
                                if (strcmp(input_file, "") == 0){
                                    //Open source file
                                    fd_input = open("/dev/null", O_RDONLY);
                                    if (fd_input == -1){
                                        printf("cannot open %s for input\n", input_file);
                                        fflush(stdout);
                                        exit(1);
                                    }
                                    //Redirect stdin to source file
                                    result = dup2(fd_input, 0);
                                    if (result == -1){
                                        perror("source dup2()");
                                        exit(2);
                                    }
                                    //Close File Descriptor On Exec
                                    fcntl(fd_input, F_SETFD, FD_CLOEXEC);
                                }

                                //redirecting output file descriptor
                                if (strcmp(output_file, "") == 0){
                                    //Open target file
                                    fd_output = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                    if (fd_output == -1){
                                        printf("cannot open %s for input\n", output_file);
                                        fflush(stdout);
                                        exit(1);
                                    }
                                    //Redirect stdout to target file
                                    result = dup2(fd_output, 1);
                                    if (result == -1) {
                                        perror("target dup2()");
                                        exit(2);
                                    }
                                    //Close File Descriptor On Exec
                                    fcntl(fd_output, F_SETFD, FD_CLOEXEC);
                                }
                            }

                            //add background and foreground
                            //execute non-built-in commands as child process
                            execvp(input[0], (char *const *) input);
                            perror(input[0]);
                            fflush(stdout);
                            exit(2);
                            break;
                        default: //parent process
                            if (bg && bg_enable){ //signal background switch
                                bg_count++; //increment bg process count
                                pid_t wait = waitpid(spawnPid, &child_status, WNOHANG); //child process in background mode
                                printf("background pid is %d\n", spawnPid);
                                bg_tracker[tracker_pos++] = spawnPid; //stores background process id into bg tracker array
                                fflush(stdout);
                            }else{
                                pid_t wait = waitpid(spawnPid, &child_status, 0); //child process in foreground mode
                                if (WIFEXITED(child_status)){
                                    //printf("child %d exited with status %d\n", wait, WEXITSTATUS(childStatus));
                                    //fflush(stdout);
                                    exit_status = WEXITSTATUS(child_status); //updates foreground last status
                                    last_signal_exit = 0;
                                }else{
                                    printf("child %d terminated by signal %d\n", wait, WTERMSIG(child_status)); //fix string add to exit_status
                                    fflush(stdout);
                                    exit_status = WTERMSIG(child_status); //updates foreground last status
                                    last_signal_exit = 1;
                                }
                            }
                    }
                }
                //reset execute flag
                exec_run = 1;
            }
        }

        //reset flag and name variables
        input_file = "";
        output_file = "";
        bg = 0;
    }
    return 0;
}
