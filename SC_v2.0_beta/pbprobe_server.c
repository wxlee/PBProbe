/*******************************************************************************************************
The PBProbe License

Copyright (c) 2012 Ling-Jyh Chen(³¯§D§Ó), Wei-Xian Lee(§õ«Â½å)

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

/*
#define PORT 149999
#define MAXBUFLEN 100000
//#define COMMANDLEN 100
#define PKSIZE 1472
#define HEADER 28

int pksize_info=-1;
int bulk_len=-1;
int max_n=-1;
float utilization=-1;
float constant_interval=-1;
float probing_rate=-1;
*/

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

int quiet=0;		//print out or not

extern char another_ip[256];

extern ExpResult local_result;
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

//enum{SER_CLI, PK_SIZE, BULK_LEN, MAX_N, UTIL, CON_INT, PROBE_RATE, VERBOSE, DEBUG};

	/*
		<< Read the control packet >>
		
		COMMAND[0]: the command is from server(99) or from client(11)
		COMMAND[1]: the packet size
		COMMAND[2]: the bulk length (K)
		COMMAND[3]: the number of packet bulk sample (MAX_N)
		COMMAND[4]: the utilization of probing traffic
		COMMAND[5]: the constant interval 
		COMMAND[6]: the probing rate
		COMMAND[7]: the verbose flag
		COMMAND[8]: the debug flag
	*/

int main(int argc, char *argv[]){
	
	int sockfd, new_sockfd, port_num=0;
	int pid;
	char temp_in[10];
	unsigned int client_length;
	
	//struct sockaddr_in server_addr, client_addr;
	struct sockaddr_in server_addr;
	int c;
	int opt = 1;
	extern int buffer_size;
	//int nZero=0;
	
	buffer_size = 1000;
	
	memset(another_ip, 0, 256);
	
	//read the parameter
	while((c=getopt(argc, argv, "vdqo:"))!= -1){
		switch(c)
		{
			case 'v':						//verbose mode
				verbose = 1;
				break;
				
			case 'd':						//debug mode
				debug = 1;
				break;
				
			case 'o':						//verbose mode
				strcpy(temp_in, optarg);
				port_num = atoi(temp_in);
				break;
				
			case 'q':
				quiet = 1;
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
	
	//solve: Address already in use
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
    
	while(1){
		if((new_sockfd = accept(sockfd, (struct sockaddr *) &client_addr, &client_length)) <0 ){
			error("ERROR on accept");
		}
		
		strcpy(another_ip, inet_ntoa(client_addr.sin_addr));
		
		//turn off Nagle Algorithm
		if(setsockopt( new_sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&opt, sizeof(opt) ) == -1){
			printf("Couldn¡¥t setsockopt(TCP_NODELAY)\n");
		}
			
		//printf("%s\t", inet_ntoa(client_addr.sin_addr));
		//fflush(stdout);
		
		pid = fork();
		
		if (pid < 0){
			error("ERROR on fork");
		}
            
		//child
        if (pid == 0)  {
            close(sockfd);
			clock_gettime(CLOCK_MONOTONIC, &start_time);  //get the start time
			handler(new_sockfd); 
			
			exit(0);
        }
		
		//parent
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
	
	FILE *logfile = fopen("measure_log", "a");
	
	extern char buffer[MAXBUFLEN];
	extern char recv_command[COMMAND_SIZE][20];
	
    int numbytes;
	extern float *COMMAND;
	extern float *RESULT;
	float *RECV_RESULT;
	
	float client_LinkCap;
	long  client_PktLoss;
	long  client_PktRecv;
	float client_PktLossRate;
	float client_Time;
	
	double total_time;
	
	int i; //for loop
	extern double coeff_var;
	extern int disp_flag;
	
	int standard_flag=0;
	
	char recv_result[RESULT_SIZE][20];
	char num_str[20];
	
	time_t  ticks;
	struct tm *t;
	char  date[20];
	
	//get the date
	ticks = time(NULL);
	t= localtime(&ticks);
	strftime(date,127,"%Y-%m-%d %H:%M:%S",t);
	
	
	for(i=0; i<COMMAND_SIZE; i++){
		memset(recv_command[i], 0, 20);
	}
	
	for(i=0; i<RESULT_SIZE; i++){
		memset(recv_result[i], 0, 20);
	}
	
	
	COMMAND = (float *)malloc(sizeof(float)*COMMAND_SIZE);
	memset(COMMAND, 0, COMMAND_SIZE);
	
	bzero(buffer,MAXBUFLEN);
		
	//rcv parameter from client request
	recv_para(sock);
	
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
	
		//set the parameter
		set_parameter();

		//run as sender first
		run_exp(1);
		
		check_control();
		
		Coeff_Var(DISP_LOG, max_n);
		
		
		if(debug==1){
			printf("Coeff_Var: %.9f\t Avg: %.9f \n", coeff_var, avg);
			fflush(stdout);
		}
		
		if(standard_flag==1) {
			control = 99;	//finish the exp
			COMMAND[SER_CLI] =  control;
			send_para(sock);
			free_it();
			break;
		}
		
		if(avg >= DISP_THRESH && coeff_var <= CV_THRESH){
			control = 99;	//finish the exp
			COMMAND[SER_CLI] =  control;
			
			send_para(sock);
			//printf("%f %f %f %f %f %f %f \n", COMMAND[0], COMMAND[1], COMMAND[2], COMMAND[3], COMMAND[4], COMMAND[5], COMMAND[6] );
			
			if(debug==1){
				//printf("Server send finish msg\n");
			}
			free_it();
			break;
		}
		
		//check dispersion
		if(avg < DISP_THRESH){
			//set the new parameter
			bulk_len *= 10;	//increase bulk length
			COMMAND[BULK_LEN] = bulk_len;
			
			
			//increase sending rate
			/*
			utilization *= SPEED_UP_FACTOR;
			COMMAND[UTIL] = utilization;
			printf("utilization: %f / %f\n", COMMAND[UTIL], utilization);
			
			if(debug==1){
				printf("Now bulk length increases to %d\n", (int)COMMAND[BULK_LEN]);
			}
			*/
			
			//reset
			coeff_var = 99999;
			//printf("%f %f %f %f %f %f %f \n", COMMAND[0], COMMAND[1], COMMAND[2], COMMAND[3], COMMAND[4], COMMAND[5], COMMAND[6] );
			
			send_para(sock);
			//printf("send para on avg\n\n");
			free_it();
			//usleep(1000);
			continue;
		}
		
		//check Cv
		if(coeff_var > CV_THRESH){
			max_n = STANDARD_MODE_NUM;
			COMMAND[MAX_NUM] = max_n;
			//COMMAND[MAX_NUM] = STANDARD_MODE_NUM;
			
			//control += 1;
			//COMMAND[SER_CLI] =  control;
			//COMMAND[SER_CLI] += 1;
			
			if(debug==1){
				printf("Standard Mode: max_n = %d\n", max_n);
			}
			
			coeff_var = 99999;
			//printf("%f %f %f %f %f %f %f \n", COMMAND[0], COMMAND[1], COMMAND[2], COMMAND[3], COMMAND[4], COMMAND[5], COMMAND[6] );
			send_para(sock);
			free_it();
			//usleep(1000);
			
			standard_flag = 1;
			continue;
		}
		
		
	}
	
	
	
	
	//receive the result from client and send the result to client

/*
	//LOCAL:::::Client to Server Result
	printf("Backward: %f %ld %ld %f %f\t", local_result.LinkCap, local_result.PktLoss, \
		local_result.PktRecv, local_result.PktLossRate, local_result.ElapsedTime);
*/	
	memset(buffer, 0, sizeof(buffer));
	
	while(1){
		if ((numbytes=recv(sock, buffer, MAXBUFLEN-1, 0)) == -1) {
			error("ERROR on recv");
		}
		
		//if(numbytes==0) printf("recv error on final result passed\n");
		
		//printf("Server result buffer: %s\n", buffer);
		
		//already rcv the result
		if(sscanf(buffer, "%s %s %s %s %s", recv_result[0], recv_result[1], \
			recv_result[2], recv_result[3], recv_result[4])!= EOF){
			
			//printf("GG2\n");
			break;
		}
		
	}
	
	
	/*
	if ((numbytes=recv(sock, buffer, MAXBUFLEN-1, 0)) == -1) {
		error("ERROR on recv");
	}
	
	if(numbytes==0) printf("recv error on final result passed\n");
	
	printf("Server result buffer: %s\n", buffer);
	
	if(sscanf(buffer, "%s %s %s %s %s", recv_result[0], recv_result[1], \
		recv_result[2], recv_result[3], recv_result[4])== EOF){
		
		printf("GG2\n");
	}
	*/	
	
	remote_result.LinkCap = atof( recv_result[0] );
	remote_result.PktLoss = atol( recv_result[1] );
	remote_result.PktRecv = atol( recv_result[2] );
	remote_result.PktLossRate = atof( recv_result[3] );
	remote_result.ElapsedTime = atof( recv_result[4] );
	
	
	usleep(1000);
	
	
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
		
		//print the remote IP
		printf("%s\t", inet_ntoa(client_addr.sin_addr));
		
		//print the exp log
		printf("%d %d\t", bulk_len, max_n);
		
		//LOCAL:::::Client to Server Result
		printf("Backward: %f %ld %ld %.2f %f\t", local_result.LinkCap, local_result.PktLoss, \
			local_result.PktRecv, local_result.PktLossRate, local_result.ElapsedTime);
		
		//REMOTE::::print out the result from client
		printf("Forward: %f %ld %ld %.2f %f [%f]\n", remote_result.LinkCap, remote_result.PktLoss, \
			remote_result.PktRecv, remote_result.PktLossRate, remote_result.ElapsedTime, total_time);
	}
	
	
	//write file
		fprintf(logfile, "%s\t%s\t %d %d\t Backward: %f %ld %ld %f %f\t Forward: %f %ld %ld %f %f Total: %f\n", date, inet_ntoa(client_addr.sin_addr),
			bulk_len, max_n, 
			local_result.LinkCap, local_result.PktLoss, local_result.PktRecv, local_result.PktLossRate, local_result.ElapsedTime, 
			remote_result.LinkCap, remote_result.PktLoss, remote_result.PktRecv, remote_result.PktLossRate, remote_result.ElapsedTime, total_time);
	
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	fclose(logfile);
	free(COMMAND);
}