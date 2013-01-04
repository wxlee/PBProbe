/*******************************************************************************************************
The PBProbe License

Copyright (c) 2012 Ling-Jyh Chen, Wei-Xian Lee

Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
and associated documentation files (the "Software"), to deal in the Software without restriction, 
including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, 
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial 
portions of the Software, and if the Software or part of it is implemented or applied in academia research 
and publication, the article "Ling-Jyh Chen, Tony Sun, Bo-Chun Wang, M. Y. Sanadidi, and Mario Gerla. 
PBProbe: A Capacity Estimation Tool for High Speed Networks. Computer Communications Journal, Elsevier, 
volume 31, number 17, pp. 3883-3893, November, 2008." related to the Software should also be properly 
cited in the reference.


THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED 
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**********************************************************************************************************/


#include "pbprobe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>
#include <sched.h>
#include <math.h>
#include <sys/wait.h>

/******************************************
Set parameter and create Disp_log array
******************************************/
void set_parameter(){
	int i;
	extern int control;
	extern int pksize_info;
	extern int bulk_len;
	extern int max_n;
	extern float utilization;
	extern float constant_interval;
	extern float probing_rate;
	extern float *COMMAND;
	extern double *DISP_LOG;
	
	control = (int)COMMAND[SER_CLI];
	pksize_info = (int)COMMAND[PK_SIZE];
	bulk_len = (int)COMMAND[BULK_LEN];
	max_n = (int)COMMAND[MAX_NUM];
	utilization = COMMAND[UTIL];
	constant_interval = COMMAND[CON_INT];
	probing_rate = COMMAND[PROBE_RATE];
	
	if(pksize_info==0){	pksize_info = PKSIZE; }
	if(bulk_len==0){ bulk_len = K;}
	if(max_n==0){ max_n = MAX_N;}
	
	//if(utilization==0){ utilization = UTILIZATION; }
	//if(constant_interval==0) {constant_interval = CONS_INTERVAL;}

	if(debug==1){
		printf("set_para: %d %d %d %d %f %f %f\n", \
			control, pksize_info, bulk_len, max_n, utilization, constant_interval, probing_rate);
			
		fflush(stdout);
	}
	
	//Disp Array
	DISP_LOG = (double *)malloc(sizeof(double)*max_n);
	
	if(DISP_LOG==NULL) {
		printf("malloc error\n");
		exit(1);
	}

	for(i=0; i<max_n; i++){
		DISP_LOG[i]=0;
	}
	
	if(debug==1){
		printf("set parameter is OK\n");
	}

}

/********************************************************
Reset the parameter
********************************************************/
void reset_parameter(){
	extern int control;
	extern int pksize_info;
	extern int bulk_len;
	extern int max_n;
	extern float utilization;
	extern float constant_interval;
	extern float probing_rate;
	//extern float *COMMAND;
	//extern double *DISP_LOG;
	
	control = 0;
	pksize_info = 0;
	bulk_len = 0;
	max_n = 0;
	utilization = 0;
	constant_interval = 0;
	probing_rate = 0;
	
	if(DISP_LOG!=NULL)
		free(DISP_LOG);
	
	if(debug==1){
		printf("reset parameter is finish\n");
	}
}

/****************************************
Free the array
****************************************/
void free_it(){

	if(DISP_LOG!=NULL)
		free(DISP_LOG);
	
}


/***************************************
TCP: Send the parameter to another node
****************************************/
void send_para(int sockfd){
	
	int i;
	char num_str[20];
	int numbytes=0;
	
	extern float *COMMAND; 
	
	memset(num_str, 0, 20);
	
	
	for(i=0; i<COMMAND_SIZE; i++){
		sprintf(num_str, "%.6f", COMMAND[i]);
		strncat(COMMAND_STR, num_str, strlen(num_str));
		
		if( i!=COMMAND_SIZE ){
			strncat(COMMAND_STR, " ", 1);
		}else{
			//strncat(COMMAND_STR, " #", 2);
			//COMMAND_STR[strlen(COMMAND_STR)] = '\n';
		}
	}
	
	
	while(1){
		if((numbytes=send(sockfd, COMMAND_STR, sizeof(COMMAND_STR) , 0)) == -1){
			error("ERROR on send");
		}
		
		if(numbytes==sizeof(COMMAND_STR)) break;
	}
	
	memset(COMMAND_STR, 0, sizeof(COMMAND_STR));
	
}



/***************************************
TCP: Recieve the parameter to another node
****************************************/
void recv_para(int sock){
	int numbytes;
	int i;
	
	extern char buffer[MAXBUFLEN];
	extern char recv_command[COMMAND_SIZE][20];
	extern float *COMMAND, *COMMAND2;
	
	
	while(1){
		//rcv the parameter from client
		if ((numbytes=recv(sock, buffer, MAXBUFLEN-1, 0)) == -1) {
			error("ERROR on recv");
		}
		
		if(numbytes==0){
			printf("rcv nothing\n");
		}

		//read in the request command from client
		if(sscanf(buffer, "%s %s %s %s %s %s %s", recv_command[0], recv_command[1], recv_command[2], 
							recv_command[3], recv_command[4], recv_command[5], recv_command[6])!= EOF){
			break;
		}
	
	}
	
	if(debug==1) printf("Rcv: %s\n", buffer);
		
	//write to commad array
	for(i=0; i<COMMAND_SIZE; i++){
		COMMAND[i] = atof( recv_command[i] );
	}
	
	if(debug==1){
		for(i=0; i<COMMAND_SIZE; i++){
			printf("%f ",COMMAND[i]);
		}
		printf("\n");
	}
	
	
	//backup the first parameter from client
	if(first_para==1){
		for(i=0; i<COMMAND_SIZE; i++){
			COMMAND2[i] = atof( recv_command[i] );
		}
	}
	
	memset(buffer, 0, sizeof(buffer));
}


/*****************************************
Average
*****************************************/
__inline__ double Avg(double num[], int size){
	int i;
    double sum = 0;
	extern double avg;
	
	for(i = 0 ; i < size ; i++ ){
		sum += num[i];
	}
    
    avg = sum/(double)size;
	
	if(debug==1){
		printf("avg1: %f \t sum:%f \t size: %d\n", avg, sum, size);
		fflush(stdout);
	} 
	
	//return avg;
}


/*****************************************
Check the main experiment is finish or not
*****************************************/
void check_control(){
	extern int control_flag;
	
	while(1){
		if(control_flag==TRUE){
			break;
		}
		
		control_flag = FALSE;
	}
}


/*****************************************
Kill unused process
*****************************************/
int kill_process(){
	//extern int g_pid_parent;	//parent process in run_exp
	extern int g_pid_recv;		//child process in run_exp
	
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000 * 50; // 50 microsec
	int status, rc_pid;
	
	
	if(g_pid_recv!=0){
		if(kill(g_pid_recv, SIGKILL)==-1) return -1;
		
		
		rc_pid = waitpid( g_pid_recv, &status, WUNTRACED | WCONTINUED);
		
		if (rc_pid == -1) {
                perror("waitpid");
                exit(EXIT_FAILURE);
        }
		
		if(debug==1){
			if (WIFEXITED(status)) {
                printf("exited, status=%d\n", WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
					printf("killed by signal %d\n", WTERMSIG(status));
			} else if (WIFSTOPPED(status)) {
					printf("stopped by signal %d\n", WSTOPSIG(status));
			} else if (WIFCONTINUED(status)) {
					printf("continued\n");
			}
		}
		
		
		
		
		if(debug==1) {
			printf("kill %d\n", g_pid_recv);
			fflush(stdout);
		}
		
		g_pid_recv = 0;
		
	}else{
		if(debug==1) {
			printf("Nothing to kill\n");
			fflush(stdout);
		}
		
	}
	
}

