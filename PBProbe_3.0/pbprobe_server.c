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
/*****************************************
Release Note:
	solve zombie issue

*****************************************/

#include "pbprobe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <math.h>
#include <netinet/tcp.h>


typedef struct {
	float	LinkCap;
	long	PktLoss;
	long	PktRecv;
	float	PktLossRate;
	float	ElapsedTime;
}ExpResult;

extern int pksize_info;				//the pksize to probe
extern int bulk_len;
extern int max_n;
extern float utilization;
extern float constant_interval;
extern float probing_rate;

extern int verbose;
extern int debug;
extern int PHASE;
extern int g_pid_parent;	//parent process in run_exp
extern int g_pid_recv;		//child process in run_exp

g_pid_parent = 0;
g_pid_recv = 0;


int quiet=0;		//print out or not

extern char another_ip[256];	//store the client ip

extern ExpResult local_result;	//the local result and remote result from client
ExpResult remote_result;

void handler(int);  //prototype

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

struct sockaddr_in client_addr;

struct timespec start_time;	//the start time
struct timespec end_time;	//the end time
extern float link_cap_temp;

/*********************************************************************************************************
	<< Read the control packet >>
	
	COMMAND[0]: the control field
	COMMAND[1]: the packet size
	COMMAND[2]: the bulk length (K)
	COMMAND[3]: the number of packet bulk sample (MAX_N)
	COMMAND[4]: the utilization of probing traffic
	COMMAND[5]: the constant interval 
	COMMAND[6]: the probing rate
	COMMAND[7]: the verbose flag
	COMMAND[8]: the debug flag
**********************************************************************************************************/

int main(int argc, char *argv[]){
	
	int sockfd, new_sockfd, port_num=0;
	int pid;
	char temp_in[10];
	unsigned int client_length;
	struct sockaddr_in server_addr;
	int c;
	int opt = 1;
	extern int buffer_size;
	
	
	buffer_size = 1040;
	
	memset(another_ip, 0, 256);
	
	//read the parameter
	while((c=getopt(argc, argv, "vbqo:"))!= -1){
		switch(c)
		{
			case 'v':						//verbose mode
				verbose = 1;
				break;
				
			case 'b':						//debug mode
				debug = 1;
				break;
				
			case 'o':						//verbose mode
				strcpy(temp_in, optarg);
				port_num = atoi(temp_in);
				break;
				
			case 'q':						//quiet mode: no output
				quiet = 1;
				break;
				
			case '?':
				printf("usage:\n");
				printf("\t-b: show debug info\n");
				printf("\t-o: the port number\n");
				printf("\t-v: show verbose info\n");
				printf("\t-q: quiet mode (WITHOUT OUTPUT ON SCREEN)\n");
				exit(1);
				break;
			
			default:
				printf("wrong command\n");
				exit(1);
		}//end switch
	}//end while
	
	//the default port
	if(port_num==0){port_num = PORT;}
	
	
	if((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1 ){
		error("ERROR on socket");
	}
	
	bzero((char *) &server_addr, sizeof(server_addr));
	
	server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_num);
	
	//solve: Address already in use error
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		
	//set the buffer
	//setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
	//setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
	
	//turn off Nagle Algorithm
	if(setsockopt( sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&opt, sizeof(opt) ) == -1){
		printf("Couldn¡¥t setsockopt(TCP_NODELAY)\n");
	}
	
	if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)  {
        error("ERROR on bind");
    }
	
	if(listen(sockfd,5) <0 ){
		error("ERROR on listen");
	}
	
	client_length = sizeof(client_addr);
    
	/************************************************************************************
		Accept the request, and fork a child process to handle this run experiment.
		When it finished, return the result and wait for next request.	
	************************************************************************************/
	while(1){
		if((new_sockfd = accept(sockfd, (struct sockaddr *) &client_addr, &client_length)) <0 ){
			error("ERROR on accept");
		}
		
		//the client IP
		strcpy(another_ip, inet_ntoa(client_addr.sin_addr));
		
		//turn off Nagle Algorithm
		if(setsockopt( new_sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&opt, sizeof(opt) ) == -1){
			printf("Couldn¡¥t setsockopt(TCP_NODELAY)\n");
		}
		
		pid = fork();	//fork child process
		
		if (pid < 0){
			error("ERROR on fork");
		}
            
		//child process: call handler to handle the experiment
        if (pid == 0)  {
            close(sockfd);
			clock_gettime(CLOCK_MONOTONIC, &start_time);  //get the start time
			handler(new_sockfd); 
			exit(0);
        }
		
		//parent process
        else{
			wait(NULL);		//wait for child process
			close(new_sockfd);
		} 
	
	}//end while(1)
	
	close(sockfd);
	

	return 0;
}//end main 


//handle the request from client
void handler(int sock){
	
	FILE *logfile = fopen("./measure_log", "a");
	
	extern char buffer[MAXBUFLEN];
	extern char recv_command[COMMAND_SIZE][20];
	
    int numbytes;
	extern float *COMMAND, *COMMAND2;
	extern float *RESULT;
	
	double total_time;
	
	extern int first_para;	//just backup the parameter at first time
	first_para = 1;
	
	int i; //for loop
	//extern double coeff_var;
	//extern int disp_flag;
	
	//int standard_flag=0;
	
	char recv_result[RESULT_SIZE][20];
	char num_str[20];
	
	time_t  ticks;
	struct tm *t;
	char  date[20];
	extern double  avg;
	
	
	//get the date
	ticks = time(NULL);
	t= localtime(&ticks);
	strftime(date,127,"%Y-%m-%d %H:%M:%S",t);
	
	
	//initial the array
	for(i=0; i<COMMAND_SIZE; i++){
		memset(recv_command[i], 0, 20);
	}
	
	for(i=0; i<RESULT_SIZE; i++){
		memset(recv_result[i], 0, 20);
	}
	
	
	COMMAND = (float *)malloc(sizeof(float)*COMMAND_SIZE);
	memset(COMMAND, 0, COMMAND_SIZE);
	
	COMMAND2 = (float *)malloc(sizeof(float)*COMMAND_SIZE);
	memset(COMMAND2, 0, COMMAND_SIZE);
	
	bzero(buffer,MAXBUFLEN);
		
	//rcv parameter from client request
	recv_para(sock);
	
	first_para=0;
	
	
	/*****
		COMMAND[0] : SER_CLI
		COMMAND[1] : PK_SIZE
		COMMAND[2] : BULK_LEN
		COMMAND[3] : MAX_NUM
		COMMAND[4] : UTIL
		COMMAND[5] : CON_INT
		COMMAND[6] : PROBE_RATE
	*****/

	//PBProbe: run as sender first =-=-=-=-=-=-=-=-=-=
	while(1){
		//=-=-=-=Phase I: tune the parameter of bulk length=-=-=-=
		if(debug==1) printf("\n\n");
		
		if(kill_process()==-1){
			if(debug==1) printf("error kill\n");
		} 
		
		set_parameter();		//set the parameter
		//run_exp(1);				//run as sender first
		//usleep(100000);
		
		if(run_exp(1)!=0) printf("error terminate\n");
	
		
		check_control();		//wait for child process finish in run_exp
		
		recv_para(sock);		//receive the parameter from client
		
		if(COMMAND[SER_CLI]!=22){	//keep in phase I, and tune the parameter of bulk length
			if(debug==1) printf("recv ok msg\n");
			continue;
		}
		
		//=-=-=-=phase II: send probing samples util converge or meet the upper-bound=-=-=-=
		if(debug==1) printf("\n\n");
		
		PHASE = 2;
		
		
		//if(kill_process()==-1){  \
			if(debug==1) printf("error kill\n"); \
		} 
		
		set_parameter();		//set the parameter
		
		if(debug==1){
			printf("Now in phaseII\n");
		}
		
		
		if(run_exp(1)!=0){
			printf("error terminate\n");
			fflush(stdout);
		} 
		
		check_control();		//wait for child process finish in run_exp
		
		if(debug==1){
			printf("Run as Sender [finish]\n");
		}
		
		break;
		
	}//end while
	
	
	
	/********************************************
	Reset the parameter and assign the command
	********************************************/
	usleep(100);
	reset_parameter();
	PHASE = 1;	
	
	//restore the parameter
	for(i=0; i<COMMAND_SIZE; i++){
		COMMAND[i] = COMMAND2[i];
	}
	
	
	
	//PBProbe: Run as Receiver =-=-=-=-=-=-=-=-=-=
	while(1){
		//=-=-=-=Phase I: tune the parameter of bulk length=-=-=-=
		set_parameter();			//set the parameter
		
		if(kill_process()==-1){
			if(debug==1) printf("error kill\n");
		} 
		
		if(run_exp(2)!=0) printf("error terminate\n");				//run as Receiver
		
		Avg(DISP_LOG, max_n);		//the avg of dispersion
		
		if(avg < DISP_THRESH){		//check the dispersion
									//set the new parameter
			bulk_len *= 10;			//increase bulk length
			COMMAND[BULK_LEN] = bulk_len;
			
			usleep(2000);
			send_para(sock);		//send parameter
			
			continue;
		}
		
		
		/****************************************
			Finish: tune the bulk length
		*****************************************/
		if(avg >= DISP_THRESH){
			control = 22;			//tune parameter finish
			COMMAND[SER_CLI] =  control;
			
			max_n = 200;			//change the max_n: the upper-bound
			COMMAND[MAX_NUM] = max_n;
			
			COMMAND[BULK_LEN] = bulk_len;
			
			usleep(2000);
			send_para(sock);
			
			
			if(debug==1){
				printf("Bulk length is OK\n");
			}
		}
		
		//=-=-=-=phase II send probing samples util converge or meet the upper-bound=-=-=-=
		PHASE = 2;
		
		set_parameter();		//set the parameter
		
		if(debug==1){
			printf("Now in phaseII\n");
		}
		
		if(kill_process()==-1){
			if(debug==1) printf("error kill\n");
		} 
		
		if(run_exp(2)!=0) printf("error terminate\n");			//run as Receiver
		check_control();		//wait for child process finish in run_exp
		
		if(debug==1){
			printf("Run as Reciever [finish]\n");
		}
		
		break;
	}//end while
	
	free_it();
	
	
	memset(buffer, 0, sizeof(buffer));
	
	
	/********************************************************************
		Receive the result from client and send the result to client
	********************************************************************/
	while(1){
		if ((numbytes=recv(sock, buffer, MAXBUFLEN-1, 0)) == -1) {
			error("ERROR on recv");
		}
		
		//already rcv the result
		if(sscanf(buffer, "%s %s %s %s %s", recv_result[0], recv_result[1], \
			recv_result[2], recv_result[3], recv_result[4])!= EOF){
			break;
		}
		
	}
	
	//the result from client
	remote_result.LinkCap = atof( recv_result[0] );
	remote_result.PktLoss = atol( recv_result[1] );
	remote_result.PktRecv = atol( recv_result[2] );
	remote_result.PktLossRate = atof( recv_result[3] );
	remote_result.ElapsedTime = atof( recv_result[4] );
	
	
	if(debug==1){
		//REMOTE::::print out the result from client
		printf("Forward: %f %ld %ld %.2f %f \n", remote_result.LinkCap, remote_result.PktLoss, \
			remote_result.PktRecv, remote_result.PktLossRate, remote_result.ElapsedTime);
	}
	
	usleep(100);
	
	//prepare the result string 
	for(i=0; i<RESULT_SIZE; i++){
		sprintf(num_str, "%.6f", RESULT[i]);
		strncat(RESULT_STR, num_str, strlen(num_str));
		
		if(i!=RESULT_SIZE){
			strncat(RESULT_STR, " ", 1);
		}else{
			//strncat(RESULT_STR, '\n', 1);
			//RESULT_STR[strlen(RESULT_STR)+1] = '\n';
		}
	}
	
	
	usleep(2000);
	
	//send result: Server to Client Exp result
	if((numbytes=send(sock, RESULT_STR, sizeof(RESULT_STR), 0)) == -1){
		error("ERROR on send");
	}
	
	clock_gettime(CLOCK_MONOTONIC, &end_time);  //get the end time
	
	total_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec)/1000000000.0;
	
	//store or print out the result
		
//=-=-=-=Print the Result-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=	
	
	if(quiet!=1){
		//print the Exp start time
		printf("%s\t",date);
		
		
		if(verbose==0){
			//print the remote IP
			printf("%s\t", inet_ntoa(client_addr.sin_addr));
		
			//the backward and forward result
			printf("Backward: %f \t", local_result.LinkCap);
			printf("Forward: %f [%f]\n", remote_result.LinkCap, total_time);
		}
		
		if(verbose==1){
			//LOCAL:::::Client to Server Result
			printf("Backward: %f %ld %ld %.2f\t", local_result.LinkCap, local_result.PktLoss, \
				local_result.PktRecv, local_result.PktLossRate);
				
			//REMOTE::::print out the result from client
			printf("Forward: %f %ld %ld %.2f [%f]\n", remote_result.LinkCap, remote_result.PktLoss, \
				remote_result.PktRecv, remote_result.PktLossRate, total_time);
				
			fflush(stdout);
		}
		
	}
	
	
	/***write file***/
	if(verbose==0){
		fprintf(logfile, "%s\t%s\t Backward: %f \t Forward: %f \t Total: %f\n", date, inet_ntoa(client_addr.sin_addr), \
			local_result.LinkCap, remote_result.LinkCap, total_time);
	}
	
	if(verbose==1){
		fprintf(logfile, "%s\t%s\t Backward: %f %ld %ld %f\t Forward: %f %ld %ld %f\t  Total: %f\n", date, inet_ntoa(client_addr.sin_addr), \
			local_result.LinkCap, local_result.PktLoss, local_result.PktRecv, local_result.PktLossRate, \
			remote_result.LinkCap, remote_result.PktLoss, remote_result.PktRecv, remote_result.PktLossRate, total_time);
	}
	
	
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	fclose(logfile);
	free(COMMAND);
	free(COMMAND2);
	free(RESULT);
}