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

	
	//Disp Array
	DISP_LOG = (double *)malloc(sizeof(double)*max_n);
	
	if(DISP_LOG==NULL) {
		printf("malloc error\n");
		exit(1);
	}

	for(i=0; i<max_n; i++){
		DISP_LOG[i]=0;
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
Send the parameter to another node
****************************************/
__inline__ void send_para(int sockfd){
	
	int i;
	char num_str[20];
	int numbytes;
	
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

	if((numbytes=send(sockfd, COMMAND_STR, sizeof(COMMAND_STR) , 0)) == -1){
		error("ERROR on send");
	}
	
	memset(COMMAND_STR, 0, sizeof(COMMAND_STR));
	
	//printf("send size: %d\n", sizeof(COMMAND_STR));
}



/***************************************
Recieve the parameter to another node
****************************************/
__inline__ void recv_para(int sock){
	int numbytes;
	int i;
	
	extern char buffer[MAXBUFLEN];
	extern char recv_command[COMMAND_SIZE][20];
	extern float *COMMAND;
	
	
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
			//printf("GG\n");
			break;
		}
	
	}
	
	
		
	//write to commad array
	for(i=0; i<COMMAND_SIZE; i++){
		COMMAND[i] = atof( recv_command[i] );
	}
	
	memset(buffer, 0, sizeof(buffer));
}

/********************************************
Coefficient of variation
********************************************/
__inline__ void Coeff_Var(double num[], int size){
	
	int i;
    double sum = 0, tmp = 0, std = 0, cv=0;
	extern double coeff_var;
	extern double avg;
	
    for(i = 0 ; i < size ; i++ ){
		sum+= num[i];
		//printf("sum: %f\t num[%d]: %f\n", sum, i, num[i]);
		//fflush(stdout);
	}
        

    avg = sum/(double)size;
    sum = 0;

    for(i = 0 ; i < size ; i++){
		sum += pow(num[i]-avg , 2);
	}
    
	//printf("sum = %.9f\n", sum);

    tmp = sum/size;
    std = sqrt(tmp);
	
	coeff_var = std/avg;
	
	//printf("avg: %.9f\t sum: %.9f\t std: %.9f\t cv: %.9f\n", avg, sum, std, coeff_var);
	
	//return cv;	
}

/*****************************************
Check the main experiment is finish or not
*****************************************/
__inline__ void check_control(){
	extern int control_flag;
	
	while(1){
		if(control_flag==TRUE){
			//printf("now in CHECK\n");
			break;
		}
		control_flag = FALSE;
	}
}