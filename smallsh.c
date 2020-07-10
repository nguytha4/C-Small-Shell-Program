/* Thanh Nguyen */
/* CS344: Spring 2019 */
/* Assignment 3: smallsh */

/***************************************************************/

#include<sys/types.h>
#include<unistd.h>
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<errno.h>   
#include<sys/wait.h> 
#include<fcntl.h>
#include<string.h>
#include<signal.h>

/***************************************************************/

/* Global array to store parsed user input */
char * token_arr[512];

/***************************************************************/

/* global variable to trigger foreground only mode */
int fgOnly = 0;

/***************************************************************/

/* Function to be used by sigaction to handle the SIGTSTP signal in the main shell */
void catchSIGTSTP(int signo) {
	
	/* if we are not currently in foreground-only mode */
	if (fgOnly == 0) {
		
		/* Let the user know we are entering foreground-only mode */
		char * enterFgoMess = "\nEntering foreground-only mode (& is now ignored)\n: ";
		write(STDOUT_FILENO, enterFgoMess, 52);
		
		/* change the flag */
		fgOnly = 1;
	}
	
	/* else, we are in background mode */
	else {
		
		/* Let the user know that we are exiting foreground-only mode */
		char * exitFgoMess = "\nExiting foreground-only mode\n: ";
		write(STDOUT_FILENO, exitFgoMess, 32);
		
		/* change the flag */
		fgOnly = 0;
	}
	
}

/***************************************************************/

/* function to replace instances of $$ with the shell's pid */
/* written with help from: https://www.intechgrity.com/c-program-replacing-a-substring-from-a-string/# */
void expansion(char * userInput, int pid) {
	
	/* variables to use to hold new string and position of substring */
	char newString[4096];
	char * pos;
	
	/* if user's input doesn't contain substring, exit function */
	if (!(pos = strstr(userInput, "$$"))) {
		return;
	}
	
	/* get the part of the string before the $$ occurrence */
	strncpy(newString, userInput, pos - userInput);
	newString[pos - userInput] = '\0';
	
	/* replace the $$ occurrence with the pid and then the rest of the original string */
	sprintf(newString+(pos - userInput), "%d%s", pid, pos + 2);
	
	/* copy the result into the original string */
	userInput[0] = 0;
	strcpy(userInput, newString);
	
	/* recurse to find other instances of $$ */
	return expansion(userInput, pid);
}

/***************************************************************/

/* Function to count the number of arguments that the user entered */
int argCount(char * userInput) {
	
	/* variable to hold a copy of the user's input */
	char buffer[4096];
	
	/* Variable to hold the number of arguments */
	int count = 0;
	
	/* copy the user's input into buffer */
	strncpy(buffer, userInput, strlen(userInput));
	
	/* variable to hold the token for strtok */
	char * token = NULL;
	
	/* start to tokenize the string */
	token = strtok(buffer, " \n");
	
	/* if the token isn't null, increase the argument acount */
	if (token != NULL)
		count++;
	
	/* get the next token */
	token = strtok(NULL, " \n");
	
	/* if the token isn't null, increase the argument count */
	if (token != NULL)
		count++;
	
	/* parse the rest of the string and increase the argument count when token isn't null */
	while(token != NULL) {
		token = strtok(NULL, " \n");
		if (token != NULL)
			count++;
	}
	
	/* return the number of arguments */
	return count;
}

/***************************************************************/

int main() {
	
	/* variable to hold the string entered by the user */
	char * lineEntered = NULL;
	
	/* variable to count the number of arguments */
	int numArgs = -5;

	/* Variables for getline */
	size_t buffersize = 4096;
	size_t characters = 0;
	
	/* token array index */
	int tokenArrIdx;
	
	/* Variable to hold the substrings as we split the read line */
	char * token = NULL;
	
	/* flag to used to see if child exited normally, initialize to true */
	int exitFlag = 1;
	
	/* variable to redirect to dev/null/ */
	int devNull;
	
	/* file in variables */
	int fileInFlag = 0;
	int fileInIndex = 0;
	char * fileIn = NULL;
	int sourceFD;
	
	/* file out variables */
	int fileOutFlag = 0;
	int fileOutIndex = 0;
	char * fileOut = NULL;
	int targetFD;
	
	/* file redirect variable */
	int result;
	
	/* background flag */
	int bgProcFlag = 0;
	
	/* last argument index */
	int lastIndex = -1;
	
	/* HOME environment variable */
	const char * HOME = getenv("HOME");
	
	/* variables to hold the exit / termination status of commands */
	int fgExitStatus = 0;
	int fgTermSignal = -5;
	int bgExitStatus = -5;
	int bgTermSignal = -5;
	
	/* get the shell's pid */
	pid_t ppid = getpid();
	
	/* array of pids */
	pid_t pid_arr[500];
	
	/* counter for valid pids */
	int pidCtr = 0;
	
	/* counter to loop through list of pids */
	int pidLoopCtr = 0;
	
	/* Child process id */
	pid_t pid = -5;
	
	/* Variable to use to check result of waitpid with WNOHANG */
	pid_t bgPid = -5;
	
	/* temp pid variable to use for swapping */
	pid_t tempPid = -5;
	
	/* Child exit status code */
	int childExitStatus = -5;
	
	/***************************************************************/
	
	/* Set up sigaction struct for SIGINT */
	struct sigaction SIGINT_action = {0};
	
	/* Initialize the variables of the SIGINT_action struct */
	SIGINT_action.sa_handler = SIG_IGN;
	SIGINT_action.sa_flags = SA_RESTART;
	sigfillset(&SIGINT_action.sa_mask);
	
	/* Create the sigaction function for the SIGINT signal using the SIGINT_action struct */
	sigaction(SIGINT, &SIGINT_action, NULL);
	
	/* Set up sigaction struct for SIGTSTP */
	struct sigaction SIGTSTP_action = {0};
	
	/* Initialize the variables of the SIGTSTP_action struct */
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigfillset(&SIGTSTP_action.sa_mask);
	
	/* Create the sigaction function for the SIGTSTP signal using the SIGTSTP_action struct */
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
	
	/* Variables to for sigprocmask */
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGTSTP);
	
	/***************************************************************/
	
	while(1)
	{
		/* Reset the token array index */
		tokenArrIdx = 1;
		
		/* Reset the last argument index */
		lastIndex = -1;
		
		/* Reset the various flags */
		fileInFlag = 0;
		fileOutFlag = 0;
		bgProcFlag = 0;
		
		/* Reset the child process id and exit status */
		pid = -5;
		childExitStatus = -5;
		
		/***************************************************************/
		
		/* Reset the loop counter to check for background child processes */
		pidLoopCtr = 0;
		
		/* check to see if any background processes have completed */
		while(pidLoopCtr < pidCtr)
		{
			
			/* check to see if bg child has finished running */
			bgPid = waitpid(pid_arr[pidLoopCtr], &childExitStatus, WNOHANG);
			
			/* if the background process has completed */
			if (bgPid != 0) {
				
				/* if the child exited normally */
				if (WIFEXITED(childExitStatus) != 0) {
					
					bgExitStatus = WEXITSTATUS(childExitStatus);
					
					printf("background pid %d is done: exit value %d\n", bgPid, bgExitStatus);
					fflush(stdout);
				}
				
				/* else the child was terminated by a signal */
				else if(WIFSIGNALED(childExitStatus) != 0) {
					
					bgTermSignal = WTERMSIG(childExitStatus);
					
					printf("background pid %d is done: terminated by signal %d\n", bgPid, bgTermSignal);
					fflush(stdout);
				}
				
				/* else the child didn't exit normally and was not terminated by a signal */
				else {
					fprintf(stderr, "program didn't terminate normally\n");  
					fflush(stdout);
				}
				
				/* swap the last valid pid with the current one */
				tempPid = pid_arr[pidCtr - 1];
				pid_arr[pidCtr - 1] = pid_arr[pidLoopCtr];
				pid_arr[pidLoopCtr] = tempPid;
				
				/* decrement the number of valid pids */
				pidCtr--;
				
				/* decrement the pidLoopCtr since we just swapped the current index with a valid pid we haven't checked yet */
				pidLoopCtr--;
			}
			
			/* increment the pidLoopCtr */
			pidLoopCtr++;
		}
		
		/***************************************************************/
		
		/* Print out a colon to the user */
		printf(": ");
		fflush(stdout);
		
		/* Get the user's input */
		characters = getline(&lineEntered, &buffersize, stdin);
		
		/* get the number of arguments */
		numArgs = argCount(lineEntered);
		
		/***************************************************************/
		
		/* look for instances of $$ and replace them with pid */
		expansion(lineEntered, ppid);
		
		/***************************************************************/
		
		/* Get the first token */
		token = strtok(lineEntered, " \n");
		
		/***************************************************************/
		
		/* If the user entered more than 2048 characters or 512 arguments */
		if (characters > 2048 || numArgs > 512) {
			
			/* Let them know and reprompt them */
			printf("You enter more than 2048 characters or more than 512 arguments.\n");
			fflush(stdout);
		}
		
		/* test with blank spaces, if NULL, prompt user again */
		else if (token == NULL) {
			;
		}
		
		/* check to see if token by itself is a # */
		else if (strcmp(token, "#") == 0) {
			;		
		}
		
		/* check to see if token starts with a # symbol */
		else if (token[0] == '#') {
			;
		}
		
		/***************************************************************/
		
		/* else, not blank or a comment */
		else {
			
			/* Save it to the first position of the global token array */
			token_arr[0] = token;
			
			/* Get the next string */
			token = strtok(NULL, " \n");
			
			/* if token isn't null, do flag checks */
			if (token != NULL) {
			
				/* Check to see if there is file in */
				if (strcmp(token, "<") == 0) {
					fileInFlag = 1;
					
					/* get the next token and input that into file in variable */
					token = strtok(NULL, " \n");
					fileIn = token;
				}
				
				/* Check to see if there is file out */
				if (strcmp(token, ">") == 0) {
					fileOutFlag = 1;
					
					/* get the next token and input that into file out variable */
					token = strtok(NULL, " \n");
					fileOut = token;
				}
				
				/* Check to see if background process invoked */
				if (strcmp(token, "&") == 0) {
										
					/* get the next token */
					token = strtok(NULL, " \n"); 
					
					/* if the token is NULL */
					if(token == NULL) {
						bgProcFlag = 1;
					}
					
					else {
						/* add the ampersand into the array */
						token_arr[tokenArrIdx] = "&";
						
						/* increment the token array index */
						tokenArrIdx++;
						
						/* Check to see if there is file in */
						if (strcmp(token, "<") == 0) {
							fileInFlag = 1;
							
							/* get the next token and input that into file in variable */
							token = strtok(NULL, " \n");
							fileIn = token;
						}
						
						/* Check to see if there is file out */
						if (strcmp(token, ">") == 0) {
							fileOutFlag = 1;
							
							/* get the next token and input that into file out variable */
							token = strtok(NULL, " \n");
							fileOut = token;
						}
					}
				}
			}
			
			/***************************************************************/
			
			/* Parse out the rest of the string */
			while (token != NULL) {
				
				/* if the file in, file out, and background flags are NOT set */
				if(fileInFlag == 0 && fileOutFlag == 0 && bgProcFlag == 0) {
					
					/* add the token into the array */
					token_arr[tokenArrIdx] = token;
				
					/* increment the token array index */
					tokenArrIdx++;	
				}
				
				/* get the next token */
				token = strtok(NULL, " \n");
				
				/* if token isn't null, do flag checks */
				if (token != NULL) {
					
					/* Check to see if there is file in */
					if (strcmp(token, "<") == 0) {
						fileInFlag = 1;
					
						/* get the next token and input that into file in variable */
						token = strtok(NULL, " \n");
						fileIn = token;
					}
					
					/* Check to see if there is file out */
					if (strcmp(token, ">") == 0) {
						fileOutFlag = 1;
					
						/* get the next token and input that into file out variable */
						token = strtok(NULL, " \n");
						fileOut = token;
					}
					
					/* Check to see if background process invoked */
					if (strcmp(token, "&") == 0) {
						
						/* get the next token */
						token = strtok(NULL, " \n"); 
						
						/* if the token is NULL */
						if(token == NULL) {
							bgProcFlag = 1;
						}
						
						else {
							/* add the ampersand into the array */
							token_arr[tokenArrIdx] = "&";
							
							/* increment the token array index */
							tokenArrIdx++;
							
							/* Check to see if there is file in */
							if (strcmp(token, "<") == 0) {
								fileInFlag = 1;
								
								/* get the next token and input that into file in variable */
								token = strtok(NULL, " \n");
								fileIn = token;
							}
							
							/* Check to see if there is file out */
							if (strcmp(token, ">") == 0) {
								fileOutFlag = 1;
								
								/* get the next token and input that into file out variable */
								token = strtok(NULL, " \n");
								fileOut = token;
							}
						}
					}
				}
			}
			
			/* Set the next argument in the token array to NULL for exec */
			token_arr[tokenArrIdx] = NULL;
			
			/***************************************************************/
			
			/* if the first command is exit */
			if(strcmp(token_arr[0], "exit") == 0) {
				
				/* if there are still background processes */
				if (pidCtr > 0) {
					
					/* kill all of the background child processes */
					for (pidLoopCtr = 0; pidLoopCtr < pidCtr; pidLoopCtr++) {
						kill(pid_arr[pidLoopCtr], SIGKILL);
					}
					
				}
				
				/* reset the sigaction struct to allow for SIGINT on the parent */
				SIGINT_action.sa_handler = SIG_DFL;
				SIGINT_action.sa_flags = SA_RESTART;
				sigfillset(&SIGINT_action.sa_mask);
	
				/* Create the sigaction function for the SIGINT signal using the SIGINT_action struct */
				sigaction(SIGINT, &SIGINT_action, NULL);
				
				/* kill the parent process */
				kill(ppid, SIGINT);
			}
			
			/* else if the first command is cd */
			else if(strcmp(token_arr[0], "cd") == 0) {
				
				/* if no path was provided, use the HOME path */
				if(token_arr[1] == NULL) {
					chdir(HOME);
				}
				
				/* else, use the path provided */
				else {
					if(chdir(token_arr[1]) < 0)
					{
						fprintf(stderr, "%s: no such file or directory\n", token_arr[1]);
						fflush(stdout);
					}
					
				}
			}
			
			/* if the first command is status */
			else if(strcmp(token_arr[0], "status") == 0) {
				
				/* if last foreground process exited normally, print its status */
				if(exitFlag == 1) {
					printf("exit value %d\n", fgExitStatus);
					fflush(stdout);
				}
				
				/* else, the last foreground process was terminated by a signal. print its status */
				else {
					printf("terminated by signal %d\n", fgTermSignal);
					fflush(stdout);
				}
			}
			
			/***************************************************************/
			
			/* else, try to parse the non-built-in command */
			else {
				
				/* fork */
				pid = fork();
				
				/* if the fork fails, notify user */
				if (pid == -1) {
					fprintf(stderr, "fork failed\n");
					fflush(stdout);
					exit(1);
				}
				
				/* child process */
				else if (pid == 0) {
										
					/* Have all children ignore the SIGTSTP signal */
					SIGTSTP_action.sa_handler = SIG_IGN;
					SIGTSTP_action.sa_flags = SA_RESTART;
					sigfillset(&SIGTSTP_action.sa_mask);
					
					/* Create the sigaction function for the SIGTSTP signal using the SIGTSTP_action struct */
					sigaction(SIGTSTP, &SIGTSTP_action, NULL);
					
					/* if the background flag is set to true and the foreground-only flag is set to false */
					if (bgProcFlag == 1 && fgOnly == 0) {
						
						/* set the child up to ignore the SIGINT signal */
						SIGINT_action.sa_handler = SIG_IGN;
						SIGINT_action.sa_flags = SA_RESTART;
						sigfillset(&SIGINT_action.sa_mask);
						
						/* Create the sigaction function for the SIGINT signal using the SIGINT_action struct */
						sigaction(SIGINT, &SIGINT_action, NULL);
					}
					
					/* else the child is a foreground process */
					else {
						
						/* set up the child to terminate on the SIGINT signal */
						SIGINT_action.sa_handler = SIG_DFL;
						SIGINT_action.sa_flags = SA_RESTART;
						sigfillset(&SIGINT_action.sa_mask);
						
						/* Create the sigaction function for the SIGINT signal using the SIGINT_action struct */
						sigaction(SIGINT, &SIGINT_action, NULL);

					}
					
					/***************************************************************/
					
					/* if the fileInFlag is set to true */
					if(fileInFlag == 1) {
						
						/* open the input source as a file and redirect it to stdin */
						sourceFD = open(fileIn, O_RDONLY);
					
						if (sourceFD == -1) { 
							fprintf(stderr, "cannot open %s for input\n", fileIn);
							fflush(stdout);
							exit(1); 
						}
						
						result = dup2(sourceFD, 0);
						if (result == -1) { 
							fprintf(stderr, "redirecting %s to stdin failed\n", fileIn);
							fflush(stdout);
							exit(1); 
						}
					}
					
					/* if there was no file specified for input and it is a background process */
					if(fileInFlag == 0 && bgProcFlag == 1 && fgOnly == 0) {
						
						/* open /dev/null and redirect stdin to it */
						devNull = open("/dev/null", O_RDONLY);
					
						if (devNull == -1) { 
							fprintf(stderr, "could not open /dev/null as read-only\n");
							fflush(stdout);
							exit(1); 
						}
						
						result = dup2(devNull, 0);
						if (result == -1) { 
							fprintf(stderr, "redirecting /dev/null to stdin failed\n");
							fflush(stdout);
							exit(1); 
						}
					}
					
					/* if the fileOutFlag is set to true */
					if(fileOutFlag == 1) {
						
						/* open the input source as a file and redirect it to stdout */
						targetFD = open(fileOut, O_WRONLY | O_CREAT | O_TRUNC, 0644);
						
						if (targetFD == -1) { 
							fprintf(stderr, "cannot open %s for output\n", fileOut);
							fflush(stdout);
							exit(1);
						}
						
						result = dup2(targetFD, 1);
						if (result == -1) { 
							fprintf(stderr, "redirecting %s to stdout failed\n", fileOut);
							fflush(stdout); 
							exit(1); 
						}
					}
					
					/* if there was no file specified for input and it is a background process */
					if(fileOutFlag == 0 && bgProcFlag == 1 && fgOnly == 0) {
						
						/* open /dev/null and redirect stdout to it */
						devNull = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
						
						if (devNull == -1) { 
							fprintf(stderr, "could not open /dev/null as write-only\n");
							fflush(stdout);
							exit(1);
						}
						
						result = dup2(devNull, 1);
						if (result == -1) { 
							fprintf(stderr, "redirecting /dev/null to stdin failed\n");
							fflush(stdout);
							exit(1); 
						}
					}
					
					/***************************************************************/
					
					/* exec */
					if(execvp(token_arr[0], token_arr) < 0) {
						fprintf(stderr, "%s: no such file or directory\n", token_arr[0]);
						fflush(stdout);
						exit(1);
					} 
					
					exit(0);
				}
				
				/* parent process */ 
				else {
										
					/* if the background flag is set to true and foreground-only is set to false */
					if (bgProcFlag == 1 && fgOnly == 0) {
					
						/* add it to the array of bg pids */
						pid_arr[pidCtr] = pid;
						
						/* increment the pid counter */
						pidCtr++;
						
						/* Display the pid of the newly created background process */
						printf("background pid is %d\n", pid);
						fflush(stdout);
					}
					
					/* if the background flag is set to flase or foreground-only flag is set to true */
					if (bgProcFlag == 0 || fgOnly == 1) {
						
						/* sigprocmask block TSTP */
						if(sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
							fprintf(stderr, "Couldn't block SIGTSTP\n");
						}
					
						/* block the parent to wait for the child foreground process */
						if (waitpid(pid, &childExitStatus, 0) > 0) { 
				  
							/* if the child exited properly */
							if (WIFEXITED(childExitStatus) != 0) {
							
								fgExitStatus = WEXITSTATUS(childExitStatus);
								
								/* set the exit flag to true so that status command prints properly */
								exitFlag = 1;
							}
							
							/* else if the child was terminated by a signal */
							else if(WIFSIGNALED(childExitStatus) != 0) {

								fgTermSignal = WTERMSIG(childExitStatus);
								
								/* Let the user know if the child was terminated by a signal in the middle of processing */
								printf("terminated by signal %d\n", fgTermSignal);
								fflush(stdout);
								
								/* set the exit flag to false so that status command prints properly */
								exitFlag = 0;
							}
							
							/* else the child did not exit properly and was not terminated by a signal */
							else {
								fprintf(stderr, "Program didn't terminate from exiting or from a signal\n");  
								fflush(stdout);
							}
						}  
						
						/* else, waitpid failed */
						else { 
							fprintf(stderr, "waitpid() failed\n"); 
							fflush(stdout);
						}
						
						/* sigprocmask unblock SIGTSTP */
						if(sigprocmask(SIG_UNBLOCK, &set, NULL) == -1) {
							fprintf(stderr, "Couldn't unblock SIGTSTP\n");
						}
					}
				}
			} 
		}
	}
	
	/***************************************************************/
	
	/* free memory taken up by the string variables */
	free(lineEntered);
}