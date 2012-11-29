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

#define K 1
#define CAP_MAX 20			//cyclic space for experiment
#define PORT 15001			//TCP: control
#define PORT_UDP 15000		//UDP: probe
#define MAXBUFLEN 100000
#define TIME_OUT_FACTOR 5	//for B_snd, if request pkt is losed, how long will resend it
#define PKSIZE 1472			
#define MAX_N 10      		// max num of probing samples
#define HEADER 28
#define CONS_INTERVAL	500	//the interval of probing traffic is constant in millisec
#define INIT_GAP 500		//millisec, the initial period of B_snd request
#define UTILIZATION	0.1	//the max utilization of probing traffic on the path, 0.01 means 1% traffic
#define SNIFF_FACTOR 1.2	//this factor is for A_snd to sniff the link speed to start a measurement
#define COMMAND_SIZE	7	//the number of command
#define RESULT_SIZE		5	//the number of result

#define SER_CLI		0
#define	PK_SIZE		1
#define	BULK_LEN	2
#define	MAX_NUM		3		
#define UTIL		4
#define	CON_INT		5
#define	PROBE_RATE	6

#define DISP_THRESH 0.001	//the threshold of dispersion, 0.001 means 1 ms
#define CV_THRESH	0.01		//the coefficient of variation (Cv = Standard Deviation / Average) 0.01 means 1%
#define STANDARD_MODE_NUM	200	//the standard mode, using 200 bulk to probe
#define SPEED_UP_FACTOR	5	//when disp is lower, increase "probing rate"

#define TRUE	1
#define FALSE	0

int control;
int pksize_info;		//the pksize to probe using UDP
int bulk_len;			//the bulk length
int max_n;				//the max sample to probe
float utilization;		//the utilization to probe
float constant_interval; //the interval between two bulks
float probing_rate;		//the sample rate to probe
float *COMMAND;
float *RESULT;			//store the result
int debug;
int verbose;
double *DISP_LOG;		//store the dispersion in exp
double coeff_var;		//the coefficient of variation of dispersion
double avg;				//the average of Dispersion
int disp_flag;			//if disp < threshold, set it to 1
int control_flag;		//check control msg is rcv or not
int finish_flag;
int buffer_size;		//TCP buffer

char another_ip[256];
char COMMAND_STR[1000];
char RESULT_STR[1000];
char buffer[MAXBUFLEN];
char recv_command[COMMAND_SIZE][20];