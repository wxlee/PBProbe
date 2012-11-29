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

/*
#define PORT 149999
#define MAXBUFLEN 100000
#define HEADER 28
#define PKSIZE 1472    //the default pkt size and control pkt size
//#define COMMANDLEN 100


/*
int		K=10;
float	utilization=0;
int		max_n=0;
float	constant_interval=0;
int 	pksize_info=0;   //send to server for request the pkt bulk with special pksize
float	probing_rate=0;
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
extern char another_ip[256];
extern float *RESULT;


extern ExpResult local_result;

ExpResult remote_result;

int		debug=0;
int		verbose=0;

void error(const char *msg){
    perror(msg);
    exit(0);
}

//enum {SER_CLI, PK_SIZE, BULK_LEN, MAX_N, UTIL, CON_INT, PROBE_RATE, VERBOSE, DEBUG};

/*
	<< Read the control packet >>
	
	COMMAND[0]: the command is from server(99) or from client(11)
	COMMAND[1]: the number of parameter
	COMMAND[2]: the packet size
	COMMAND[3]: the bulk length (K)
	COMMAND[4]: the number of packet bulk sample (MAX_N)
	COMMAND[5]: the utilization of probing traffic
	COMMAND[6]: the constant interval 
	COMMAND[7]: the verbose flag
	COMMAND[8]: the debug flag
*/

int main(int argc, char *argv[]){

	int sockfd, port_num, n;
    struct sockaddr_in server_addr;
    struct hostent *server;
	int c;
	char hostname[256];
	char buffer[MAXBUFLEN];
	int numbytes;
	extern float *COMMAND;
	extern float *RESULT;
	extern int finish_flag;
	extern int buffer_size;
	
	buffer_size = 1000;
	
	
	float *RECV_RESULT;
	char temp_in[10];
	int i; //for loop
	char num_str[20];
	int series_num=0;
	
	float server_LinkCap;
	long server_PktLoss;
	long server_PktRecv;
	float server_PktLossRate;
	float server_Time;
	int opt = 1;
	char recv_result[RESULT_SIZE][20];
	
	time_t  ticks;
	struct tm *t;
	char  date[20];
	
	for(i=0; i<RESULT_SIZE; i++){
		memset(recv_result[i], 0, 20);
	}
	
	memset(num_str, 0, 20);
	memset(another_ip, 0, 256);
	memset(hostname, 0, 256);
	
	
	ticks = time(NULL);
	t= localtime(&ticks);
	strftime(date,127,"%Y-%m-%d %H:%M:%S",t);
	//printf("%s\t",date);
	//fflush(stdout);
	
	while((c=getopt(argc, argv, "s:p:o:k:u:n:c:r:vb "))!= -1){
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
				
			case 'p':	//the size of packet included header
				strcpy(temp_in,optarg);
				pksize_info = atoi(temp_in);
				
				if(pksize_info < 28) {
					printf("Packet size error. (size >= 28)\n");
					exit(1);
				}
				
				pksize_info -= HEADER; 
				break;
				
			case 'k':	//the length of bulk
				strcpy(temp_in,optarg);
				bulk_len = atoi(temp_in);
				break;
				
			case 'u':	//the utilization of probing traffic
				strcpy(temp_in,optarg);
				utilization = atof(temp_in);
				break;
				
			case 'n':	//the number of probing bulks
				strcpy(temp_in,optarg);
				max_n = atoi(temp_in);
				break;
				
			case 'c':	//the interval of probing samples in ms
				strcpy(temp_in,optarg);
				constant_interval = atof(temp_in);
				break;
				
			case 'r':	//the rate of probing traffic
				strcpy(temp_in,optarg);
				probing_rate = atof(temp_in);
				break;
				
			case 'b':	//debug flag
				debug = 1;
				break;
			
			case '?':
				printf("\tusage:\n");
				printf("\t-s: server ip\n");
				printf("\t-p: the size of packet, ex: -p 1500\n");
				printf("\t-c: the type of probing traffic, ex: '-c 100' means constant interval 100 ms\n");
				printf("\t-k: the length of bulk, ex: -k 10\n");
				printf("\t-u: the utilization of link capacity, ex: -u 0.01\n");
				printf("\t-n: the number of probing bulks, ex: -n 200\n");
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

	strcpy(another_ip, inet_ntoa(server_addr.sin_addr));
	
	COMMAND = (float *)malloc(sizeof(float)*COMMAND_SIZE);
	memset(COMMAND,'1', COMMAND_SIZE);
	
	
	COMMAND[SER_CLI] 	= 0;
	COMMAND[PK_SIZE]	= pksize_info;
	COMMAND[BULK_LEN]	= bulk_len;
	COMMAND[MAX_NUM]	= max_n;
	COMMAND[UTIL]		= utilization;
	COMMAND[CON_INT]	= constant_interval;
	COMMAND[PROBE_RATE] = probing_rate;
	
	//send the parameter to server
	send_para(sockfd);

	//PBProbe: run as Receiver first =-=-=-=-=-=-=-=-=-=
	while(1){
		//set the parameter
		set_parameter();
		
		//run as Receiver first
		run_exp(2);
				
		check_control();
		
		recv_para(sockfd);
		//printf("client rcv para\n");
		//printf("%f %f %f %f %f %f %f \n\n", COMMAND[0], COMMAND[1], COMMAND[2], COMMAND[3], COMMAND[4], COMMAND[5], COMMAND[6] );
		
		if(COMMAND[SER_CLI]==99){
			if(debug==1){
				printf("finish\n");
			}
			free_it();
			break;
		}
		
		free_it();		
	}
	
	
	
	//receive the result from Server and send the result to Server

/*	
	//LOCAL:::::Server to Client Result
	printf("Download: %f %ld %ld %f %f\t", local_result.LinkCap, local_result.PktLoss, \
		local_result.PktRecv, local_result.PktLossRate, local_result.ElapsedTime);
*/	
	
	
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
	
	//printf("client result: %s\n", RESULT_STR);
	
	//send result: Server to Client Exp result
	if((numbytes=send(sockfd, RESULT_STR, sizeof(RESULT_STR), 0)) == -1){
		error("ERROR on send");
	}
	
	
	
	usleep(1000);
	memset(buffer, 0, sizeof(buffer));
	
	while(1){
		//recv result: Client to Server Exp result
		if ((numbytes=recv(sockfd, buffer, MAXBUFLEN-1, 0)) == -1) {
			error("ERROR on recv");
		}
		
		//already rcv the result
		if(sscanf(buffer, "%s %s %s %s %s", recv_result[0], recv_result[1], \
			recv_result[2], recv_result[3], recv_result[4])!= EOF){
			
			//printf("GG2\n");
			break;
		}
	
	}
	
	
	
	remote_result.LinkCap = atof( recv_result[0] );
	remote_result.PktLoss = atol( recv_result[1] );
	remote_result.PktRecv = atol( recv_result[2] );
	remote_result.PktLossRate = atof( recv_result[3] );
	remote_result.ElapsedTime = atof( recv_result[4] );
	
	
//=-=-=-=Print the Result-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
	
	//print the Exp time
	printf("%s\t",date);
	
	//LOCAL:::::Server to Client Result
	printf("Download: %f %ld %ld %.2f %f\t", local_result.LinkCap, local_result.PktLoss, \
		local_result.PktRecv, local_result.PktLossRate, local_result.ElapsedTime);
	
	//REMOTE::::print out the result from Server
	printf("Upload: %f %ld %ld %.2f %f\n", remote_result.LinkCap, remote_result.PktLoss, \
		remote_result.PktRecv, remote_result.PktLossRate, remote_result.ElapsedTime);
		
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=	
	
	
	free(RESULT);
	free(COMMAND);
	close(sockfd);
	
}//end main















