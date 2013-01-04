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
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
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
extern char another_ip[256];
//extern float *RESULT;
extern int PHASE;
extern float link_cap_temp;
//phase = 1;				//run phase 1 first to tune the parameter

extern ExpResult local_result;

ExpResult remote_result;

int		debug=0;
int		verbose=0;
//double  avg = 0;

void error(const char *msg){
    perror(msg);
    exit(0);
}


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
	FILE *logfile = fopen("./measure_log", "a");
	
	int sockfd, port_num, n;
    struct sockaddr_in server_addr;
    struct hostent *server;
	int c;
	char hostname[256];
	char buffer[MAXBUFLEN];
	int numbytes;
	extern float *COMMAND, *COMMAND2;
	extern float *RESULT, *RESULT2;
	extern int finish_flag;
	extern int buffer_size;
	extern double *DISP_LOG;
	extern double avg;
	
	buffer_size = 1040;
	
	extern int g_pid_parent;	//parent process in run_exp
	extern int g_pid_recv;		//child process in run_exp

	g_pid_parent = 0;
	g_pid_recv = 0;
	
	struct timespec start_time;	//the start time
	struct timespec end_time;	//the end time
	double total_time;			//exp time
	char temp_in[10];
	int i; 						//for loop
	char num_str[20];
	
	
	int opt = 1;
	char recv_result[RESULT_SIZE][20];
	extern int first_para;		//client node should not change this value
	first_para = 0;
	
	
	time_t  ticks;
	struct tm *t;
	char  date[20];
	
	//initial
	for(i=0; i<RESULT_SIZE; i++){
		memset(recv_result[i], 0, 20);
	}
	
	memset(num_str, 0, 20);
	memset(another_ip, 0, 256);
	memset(hostname, 0, 256);
	
	//get the time
	ticks = time(NULL);
	t= localtime(&ticks);
	strftime(date,127,"%Y-%m-%d %H:%M:%S",t);
	
	while((c=getopt(argc, argv, "s:p:o:vb"))!= -1){
		switch(c)
		{
			case 's':						//to server side ip
				strcpy(hostname,optarg);
				break;
			
			case 'v':						//verbose mode
				verbose = 1;
				break;
				
			case 'o':						//port
				strcpy(temp_in, optarg);
				port_num = atoi(temp_in);
				break;
			
			case 'p':						//the size of packet included header
				strcpy(temp_in,optarg);
				pksize_info = atoi(temp_in);
				
				if(pksize_info < 28) {
					printf("Packet size error. (size >= 28)\n");
					exit(1);
				}
				
				pksize_info -= HEADER; 
				break;
			
			case 'b':						//debug flag
				debug = 1;
				break;
			
			
			case '?':
				printf("usage:\n");
				printf("\t-s: server ip\n");
				printf("\t-p: the size of packet, ex: -p 1500\n");
				printf("\t-o: the port number\n");
				printf("\t-v: show verbose log\n");
				printf("\t-b: show debug info\n");
				exit(1);
				break;
			
			default:
				printf("wrong command\n");
				exit(1);
		}//end switch
	}//end while
	
	
	//the default
	if(port_num==0){port_num = PORT;}
	if(pksize_info==0) {pksize_info = PKSIZE;}
	
	//TCP
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ){	error("ERROR on socket");	}
	
	if ((server=gethostbyname(hostname)) == NULL) {  // get the host info
        error("ERROR: You don't assign the Server IP");
    }
	
	//set the buffer
	//setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
	//setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
	
	//solve: Address already in use error
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	
	//turn off Nagle Algorithm
	if(setsockopt( sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&opt, sizeof(opt) ) == -1){
		printf("Couldn¡¥t setsockopt(TCP_NODELAY)\n");
	}
	
	server_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length);
    server_addr.sin_port = htons(port_num);

	
    if (connect(sockfd,(struct sockaddr *) &server_addr,sizeof(server_addr)) < 0){
		error("ERROR: Server side is unreachable!! ");
	}
	
	clock_gettime(CLOCK_MONOTONIC, &start_time);  //get the start time
	
	strcpy(another_ip, inet_ntoa(server_addr.sin_addr));
	
	//initial
	COMMAND = (float *)malloc(sizeof(float)*COMMAND_SIZE);
	memset(COMMAND, 0, COMMAND_SIZE);
	COMMAND2 = (float *)malloc(sizeof(float)*COMMAND_SIZE);
	memset(COMMAND2, 0, COMMAND_SIZE);
	RESULT2 = (float *)malloc(sizeof(float)*RESULT_SIZE);
	memset(RESULT2, 0,RESULT_SIZE);
	
	COMMAND[SER_CLI] 	= 0;
	COMMAND[PK_SIZE]	= pksize_info;
	COMMAND[BULK_LEN]	= bulk_len;
	COMMAND[MAX_NUM]	= max_n;
	COMMAND[UTIL]		= utilization;
	COMMAND[CON_INT]	= constant_interval;
	COMMAND[PROBE_RATE] = probing_rate;
	
	//backup
	for(i=0; i<COMMAND_SIZE; i++){
		COMMAND2[i] = COMMAND[i];
	}
	
	//send the parameter to server
	send_para(sockfd);
	
	
	//PBProbe: run as receiver first =-=-=-=-=-=-=-=-=-=
	while(1){
		//=-=-=-=Phase I: tune the parameter of bulk length=-=-=-=
		
		if(debug==1) printf("\n\n");
		
		if(kill_process()==-1){
			if(debug==1) printf("error kill\n");
		} 
		
		set_parameter();					//set the parameter
		//run_exp(2);							//run as Receiver
		
		
		if(run_exp(2)!=0) printf("error terminate\n");
		
		//check_control();					//wait for child process finish in run_exp
		Avg(DISP_LOG, max_n);				//the avg of dispersion
		//free_it();
		
		if(debug==1) {
			printf("avg: %f\n", avg);
			fflush(stdout);
		}
		
		if(avg < DISP_THRESH){				//check the dispersion
											//set the new parameter
			bulk_len *= 10;					//increase bulk length
			COMMAND[BULK_LEN] = bulk_len;
			
			usleep(2000);
			send_para(sockfd);				//send the parameter
			
			//free_it();
			continue;
		}
		
		
		/*********************************************
			Finish: tune the bulk length
		*********************************************/
		
		if(avg >= DISP_THRESH){
			control = 22;			//tune parameter finish
			COMMAND[SER_CLI] =  control;
			
			max_n = 200;			//change the max_n
			COMMAND[MAX_NUM] = max_n;
			
			COMMAND[BULK_LEN] = bulk_len;
			
			usleep(2000);
			send_para(sockfd);
			
			if(debug==1){
				printf("Bulk length is OK\n");
			}
		}
		
	
		//=-=-=-=phase II send probing samples util converge or meet the upper-bound=-=-=-=
		if(debug==1) printf("\n\n");
		
		PHASE = 2;
		
		if(kill_process()==-1){ \
			if(debug==1) printf("error kill\n"); \
		} 
		
		set_parameter();
		
		if(debug==1){
			printf("Now in phaseII\n");
			//printf("probing_rate: %f\n", COMMAND[PROBE_RATE]);
		}
		
		if(run_exp(2)!=0) printf("error terminate\n");
		
		check_control();				//wait for child process finish in run_exp
		
		if(debug==1){
			printf("Run as Receiver [finish]\n");
		}
		
		for(i=0; i<RESULT_SIZE; i++){	//backup the result at RESULT2
			RESULT2[i] = RESULT[i];
		}
		
		break;
	}//end whie
	
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
	
	
	
	//PBProbe: Run as Sender =-=-=-=-=-=-=-=-=-=
	while(1){
		//=-=-=-=Phase I: tune the parameter of bulk length=-=-=-=
		
		set_parameter();					//set the parameter
		
		if(kill_process()==-1){
			if(debug==1) printf("error kill\n");
		} 
		
		if(run_exp(1)!=0) printf("error terminate\n");							//run as sender
		check_control();
		recv_para(sockfd);					//receive the parameter from server

		if(COMMAND[SER_CLI]!=22){			//keep in phase I, and tune the parameter of bulk length
			continue;
		}
		
		//=-=-=-=phase II send probing samples util converge or meet the upper-bound=-=-=-=
		PHASE = 2;
		
		if(debug==1){
			printf("Now in phaseII\n");
		}
		
		set_parameter();
		
		if(kill_process()==-1){
			if(debug==1) printf("error kill\n");
		} 
		
		if(run_exp(1)!=0) printf("error terminate\n");
		
		check_control();
		
		if(debug==1){
			printf("Run as Sender [finish]\n");
		}
		
		break;
		
	}//end while
	
	free_it();
	
	

	
	if(verbose==1){
		//LOCAL:::::Server to Client Result
		printf("Download: %f %ld %ld %f %f\t", local_result.LinkCap, local_result.PktLoss, \
			local_result.PktRecv, local_result.PktLossRate, local_result.ElapsedTime);
	
	}
	
	//receive the result from Server and send the result to Server
	for(i=0; i<RESULT_SIZE; i++){
		sprintf(num_str, "%.6f", RESULT2[i]);
		strncat(RESULT_STR, num_str, strlen(num_str));
		
		if(i!=RESULT_SIZE){
			strncat(RESULT_STR, " ", 1);
		}else{
			//strncat(RESULT_STR, '\n', 1);
			//RESULT_STR[strlen(RESULT_STR)+1] = '\n';
		}
	}
	
	
	//send result: Server to Client Exp result
	if((numbytes=send(sockfd, RESULT_STR, sizeof(RESULT_STR), 0)) == -1){
		error("ERROR on send");
	}
	
	if(debug==1){
		printf("%s\n",RESULT_STR);
	}
	
	
	//usleep(100);
	memset(buffer, 0, sizeof(buffer));
	
	while(1){
		//recv result: Client to Server Exp result
		if ((numbytes=recv(sockfd, buffer, MAXBUFLEN-1, 0)) == -1) {
			error("ERROR on recv");
		}
		
		if(numbytes==0) continue;
		
		//already rcv the result
		if(sscanf(buffer, "%s %s %s %s %s", recv_result[0], recv_result[1], \
			recv_result[2], recv_result[3], recv_result[4])== RESULT_SIZE){
			break;
		}
	}//end while
	
	
	//the result from server
	remote_result.LinkCap = atof( recv_result[0] );
	remote_result.PktLoss = atol( recv_result[1] );
	remote_result.PktRecv = atol( recv_result[2] );
	remote_result.PktLossRate = atof( recv_result[3] );
	remote_result.ElapsedTime = atof( recv_result[4] );
	
	clock_gettime(CLOCK_MONOTONIC, &end_time);  //get the end time
	total_time = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec)/1000000000.0;
	
//=-=-=-=Print the Result-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	
	/*************************
	  Output to screen
	*************************/
	printf("%s\t",date);		//print the Exp time
	
	if(verbose==0){
		printf("Download: %f \t", local_result.LinkCap);
		printf("Upload: %f [%f]\n", remote_result.LinkCap, total_time);
	}
	
	if(verbose==1){
		//LOCAL:::::Server to Client Result
		printf("Download: %f %ld %ld %.2f\t", local_result.LinkCap, local_result.PktLoss, \
			local_result.PktRecv, local_result.PktLossRate);
		
		//REMOTE::::print out the result from Server
		printf("Upload: %f %ld %ld %.2f [%f]\n", remote_result.LinkCap, remote_result.PktLoss, \
			remote_result.PktRecv, remote_result.PktLossRate, total_time);
		
	}
	
	/************************
		Write file
	************************/
	if(verbose==0){
		fprintf(logfile, "%s\t Download: %f\t  Upload: %f\t Total: %f\n", date,
			local_result.LinkCap, remote_result.LinkCap, total_time);	
	}
	
	if(verbose==1){
		fprintf(logfile, "%s\t Download: %f %ld %ld %f\t  Upload: %f %ld %ld %f\t Total: %f\n", date, \
			local_result.LinkCap, local_result.PktLoss, local_result.PktRecv, local_result.PktLossRate,  \
			remote_result.LinkCap, remote_result.PktLoss, remote_result.PktRecv, remote_result.PktLossRate, total_time);
	}
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=	
	
	fclose(logfile);
	free(RESULT);
	free(COMMAND);
	free(RESULT2);
	free(COMMAND2);
	close(sockfd);
	
}//end main















