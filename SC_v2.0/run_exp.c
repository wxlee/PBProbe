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

extern void error(const char *msg);

/*define in pbprobe.h*/
extern int pksize_info;				//the pksize to probe
extern int bulk_len;
extern int max_n;
extern float utilization;
extern float constant_interval;
extern float probing_rate;
extern int debug;
extern int verbose;
extern char another_ip[256];
extern float *RESULT;
extern int finish_flag;

finish_flag = FALSE;

typedef struct {
	float	LinkCap;
	int		PktLoss;
	int		PktRecv;
	float	PktLossRate;
	float	ElapsedTime;
}ExpResult;


ExpResult local_result;



struct shared {
	double cap_send[CAP_MAX];		//request time at B_snd
	double cap_recv1[CAP_MAX];		//the time of rcv first pkt of K
	double cap_recv2[CAP_MAX];		//the time of rcv last pkt of K
	int cap_k[CAP_MAX];				//the length of bulk
	int cap_last_k[CAP_MAX];		//for checking the rcv pkt continuous or not
	double cap_C;					//the temp Link Capacity in Experiment
	double cap_RTT_SUM;				//save the temp smallest RTT_SUM
	//int cap_index;				
	int n;							//the num of good samples
	long mode;
	int cur_mode;
	double G;						//the sleep period between two bulks
	
	//wxlee
	int request_num;				//the num of request from B_snd to A_recv, and changed by B_recv
	int waiting_flag;				//waiting to the receiver to receive pkt, than request a new one
	int pksize_info;
	float utilization;				//the utilization of probing traffic
	int max_n;						//the number of probing bulks

};


struct sockaddr_in addr_f; // connector's address information
struct sockaddr_in addr_b; // connector's address information

int shm_init(struct shared *info) {
int i;
	info->cap_RTT_SUM = 1000000000;
	for (i=0;i<CAP_MAX;i++){
		info->cap_send[i] = -1;
		info->cap_recv1[i] = -1;
		info->cap_recv2[i] = -1;
	}
	info->n = 0;
	
	//wxlee
	info->request_num = 1;		//the num of request bulk
	info->waiting_flag = 0;		//1: wait
	info->pksize_info = 0;
	info->max_n = 0;
		
    return 0;
}


int A_send(int send_socket, struct shared *info, int m);
int A_recv(int send_socket, int recv_socket, struct shared *info, int m);
int B_send(int send_socket, struct shared *info, int m);
int B_recv(int send_socket, int recv_socket, struct shared *info, int m);




int run_exp(int type){
	int send_socket, recv_socket;
	struct hostent *he;
	int pid_recv;
	
	int opt = 1;
    int port = -1;
	char   *shmptr;
	int    shmid;
	struct shmid_ds buf;
	struct shared *info;
	long mode;

    struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000 * 50; // 50 microsec
	
	mode = (long)type;
	
	RESULT = (float *)malloc(sizeof(float)*RESULT_SIZE);
	memset(RESULT, '1',RESULT_SIZE);
	
	control_flag = FALSE;
	
	if ((he=gethostbyname(another_ip)) == NULL) {  // get the host info
        perror("gethostbyname");
        exit(1);
    }
	
	if ((send_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		error("socket");
	}

	if ((recv_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		error("socket");
	}

	setsockopt(recv_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		
	
	/* for FORWARD flow */
        addr_f.sin_family = AF_INET;   		// host byte order
        addr_f.sin_port = htons(PORT_UDP); 		// short, network byte order
        addr_f.sin_addr = *((struct in_addr *)he->h_addr);
        memset(&(addr_f.sin_zero), '\0', 8); 	// zero the rest of the struct
	/* for BACKWARD flow */
        addr_b.sin_family = AF_INET;   		// host byte order
        addr_b.sin_port = htons(PORT_UDP); 		// short, network byte order
        addr_b.sin_addr.s_addr = INADDR_ANY;
        memset(&(addr_b.sin_zero), '\0', 8); 	// zero the rest of the struct
        if (bind(recv_socket, (struct sockaddr *) &addr_b, sizeof(addr_b)) != 0) {
                perror("bind");
                abort();
        }

        shmid = shmget (IPC_PRIVATE, sizeof(struct shared), SHM_R | SHM_W) ;
	if (shmid == -1) {
		perror("shmget");
		exit(1);
	}
        shmptr = shmat(shmid,0,0);
	if (shmptr == NULL) {
		perror("shmat");
		exit(1);
	}

    info = (struct shared *) shmptr;
    shmctl(shmid, IPC_RMID, &buf);

	info->mode = mode;
	
	
	if((constant_interval!=0 && probing_rate!=0) || (utilization!=0 && constant_interval!=0) ||(utilization!=0 && probing_rate!=0)){
		printf("Parameter error. You can't use '-c', '-r' and '-u' at the same time.\n");
		exit(1);
	}
	
	//the default value
	utilization = UTILIZATION;
	
	pid_recv = fork ();
	//printf("in run_exp\n");
	
	
	
	//FIFO
	{
		if(geteuid() == 0) 
		{
			struct sched_param schedParam;
			//* fprintf(stderr, "setting SCHED_FIFO\n"); 
			schedParam.sched_priority = 1;
			if(sched_setscheduler(0, SCHED_FIFO, &schedParam) != 0) {
			perror("setscheduler:");
			}
		}
	}
	
	
	info->cur_mode = 1;

	info->G = INIT_GAP; // ms
	info->n = 0;

	if (info->mode==1) {		// this is A
		// PBProbe Phase 1: forward direction estimation
		if (pid_recv==0){
                	A_send(send_socket, info, 1);
		} else {
                	A_recv(send_socket, recv_socket, info, 3);
		}
		
		///////cur_mode = 5, finish forward direction
		while (info->cur_mode < 5){
			nanosleep(&ts,NULL);    // sleep for 50ms
		}

		// GBProbe Phase 2: backward direction estimation
		if (pid_recv==0){
                	B_send(send_socket, info, 5);
		} else {
                	B_recv(send_socket, recv_socket, info, 6);
                	kill(pid_recv, SIGKILL);
		}
	} else if (info->mode==2) {	// this is B
		// PBProbe Phase 1: forward direction estimation
		if (pid_recv==0){
			int addr_len;
			int numbytes;
			char sbuf[MAXBUFLEN];
			int *DATA;
			int t;
        		addr_len = sizeof(struct sockaddr);
				
			//rcv the START pkt from A_snd
			while (1) {
				if ((numbytes=recvfrom(recv_socket, sbuf, MAXBUFLEN-1, 0, (struct sockaddr *)&addr_b, &addr_len)) == -1) {
					perror("recvfrom");
					exit(1);
				}
				if (numbytes != pksize_info){
					//printf("this is B\b");
					printf("Packet size error\n");
					//exit(1);
					continue;
				}
				DATA = sbuf;
				t = DATA[0];	//the START pkt DATA[0] = 1
				// printf("t=%d %d %d %d\n",DATA[0],DATA[1],DATA[2],DATA[3]);
				if (t==1) {
					info->cur_mode = 3;		//cur_mode = 3, A_rcv start to send bulk to B_rcv
					break;
				}
			}

                	B_send(send_socket, info, 2);
		} else {
                	B_recv(send_socket, recv_socket, info, 3);
		}

		// GBProbe Phase 2: backward direction estimation
		if (pid_recv==0){
			// do nothing.....
                	// A_send(send_socket, info);
		} else {
                	A_recv(send_socket, recv_socket, info, 6);
                	kill(pid_recv, SIGKILL);
		}

		//A_rcv change the cur_mode = 8, finish two direction!!
		while (info->cur_mode < 8){
			nanosleep(&ts,NULL);    // sleep for 50ms
		}

	} else {		// impossible to enter here.....
        	if (pid_recv == 0) {
			fprintf(stderr,"Mode (mode=%ld) is incorrect!\n", info->mode);
		} else {
                	kill(pid_recv, SIGKILL);
		}

	}

    shmdt(shmptr);
	control_flag = TRUE;
    //exit(0);
	return 0;
	
}

//START pkt-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
int A_send(int send_socket, struct shared *info, int m) {
int *DATA;
int numbytes;
struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000 * 100; // 100 microsec
extern int pksize_info;
unsigned long temp_interval;
unsigned int big_sleep;

/*
	//default or new pksize
	pksize = PKSIZE;
	if(info->pksize_info!=0)
		pksize = info->pksize_info;
	//
*/

	
	if(probing_rate != 0){
		temp_interval = ((pksize_info+HEADER)*8*bulk_len/probing_rate)*1000;
		ts.tv_sec = (int)(temp_interval/1000000000.0);
		ts.tv_nsec = (int)temp_interval%1000000000;	
	}
	
	if(constant_interval!=0){
		temp_interval = constant_interval*1000000;		//millisec to nanosec
		ts.tv_sec = (int)(temp_interval/1000000000.0);
		ts.tv_nsec = (int)temp_interval%1000000000;	
	}
	
	big_sleep = (int)(temp_interval/1000000000.0);
	
	
	while (info->cur_mode == m){
		DATA = (int *)malloc(pksize_info);
		memset(DATA,'1',pksize_info);
		DATA[0] = m;
		if ((numbytes=sendto(send_socket, DATA, pksize_info, 0, (struct sockaddr *)&addr_f, sizeof(struct sockaddr))) == -1) {
			perror("sendto");
			free(DATA);
			exit(1);
		}
		free(DATA);
		
		if(big_sleep>=1){
			sleep(big_sleep);
			nanosleep(&ts,NULL);
		}else{
			nanosleep(&ts,NULL); // sleep for 10 microsec
		}
		
		/*
		//slow the START packet rate
		{
			temp_interval *= SNIFF_FACTOR;
			ts.tv_sec = (int)(temp_interval/1000000000.0);
			ts.tv_nsec = (int)temp_interval%1000000000;	
			big_sleep = (int)(temp_interval/1000000000.0);
		}
		*/
	}
	//printf("A_snd finish.\n");
	return 0;
}


//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
int A_recv(int send_socket, int recv_socket, struct shared *info, int m) {
char buf[MAXBUFLEN];
int addr_len;
int numbytes;
int *DATA;
int i, t, n, p;
struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000 * 10; // 10 microsec
	addr_len = sizeof(struct sockaddr);
	n = 0;
	
int request_num;	//receive the num from B_snd
int pkt_num = 1; 
int bulk_num = 1;
extern int pksize_info;
extern int max_n;

/*
//default or new pksize
pksize = PKSIZE;
if(info->pksize_info!=0)	pksize = info->pksize_info;
max_n = MAX_N;
if(info->max_n!=0) max_n = info->max_n;
*/

	
	while (1) {
	
		// ytls
		if ((numbytes=recvfrom(recv_socket, buf, MAXBUFLEN-1, 0, (struct sockaddr *)&addr_b, &addr_len)) == -1) {
			perror("recvfrom");
			exit(1);
		}
		
		if (numbytes != pksize_info){
			continue;
		}
		
		/*
		if(debug==1){
			printf("A_rcv: No. %d\n", request_num);
			fflush(stdout);
		}
		*/
		
		
		DATA = buf;

		t = DATA[0];
		p = DATA[1];
		request_num = DATA[4];	
		
	/*	
		if(debug==1){
			printf("A_rcv: No. %d\n", request_num);
			fflush(stdout);
		}
	*/	
		
		//???? when to use ????
		while(t==3)
		{
			nanosleep(&ts,NULL);	// 10 microsec
			DATA[0] = 4;

			if ((numbytes=sendto(send_socket, DATA, pksize_info, 0, (struct sockaddr *)&addr_f, sizeof(struct sockaddr))) == -1) {
				perror("A_recv : sendto");
				free(DATA);
				exit(1);
			}

			if ((numbytes=recvfrom(recv_socket, buf, MAXBUFLEN-1, 0, (struct sockaddr *)&addr_b, &addr_len)) == -1) {
				perror("recvfrom");
				exit(1);
			}
			if (numbytes != pksize_info){
				continue;
			}
			DATA = buf;

			t = DATA[0];
			p = DATA[1];
			
			//wxlee
			request_num = DATA[4];
			
			/*
			if(debug==1){
				printf("A_rcv secret area\n");
				fflush(stdout);
			}
			*/
				
			//printf("A_rcv req_num: %d\n", request_num);
			//fflush(stdout);
		}
		
		
		//rcv the request from B_snd, and send a bulk!!
		//[forward]  B_snd: DATA[0]=t=2, A_rcv: m=3
		//[backward] B_snd: DATA[0]=t=5, A_rcv: m=6
		if (t==m-1) {
			//count++;
			n++;
			n %= CAP_MAX;
			
			/*
			if(debug==1){
				printf("A_rcv rcv No. %d request from B_send\n", request_num);
				fflush(stdout);
			}
			*/
	
			if (info->cur_mode < m) info->cur_mode = m;	// now, it's type III packet

			DATA = (int *)malloc(pksize_info);
			memset(DATA,'1',pksize_info);
			for (i=0;i<=bulk_len;i++){
				
				DATA[0] = m;	//mode
				DATA[1] = p;	//the num of CAP_MAX
				DATA[2] = bulk_len;	//the length of pkt bulk
				DATA[3] = i;	//the series num of K
				
				//wxlee
				DATA[4] = request_num;	//the num of bulk
				DATA[5] = i;			//the series num of K
				DATA[6] = pkt_num;		//the num of pkt send to B_rcv from num. 0
				
				if ((numbytes=sendto(send_socket, DATA, pksize_info, 0, (struct sockaddr *)&addr_f, sizeof(struct sockaddr))) == -1) {
					perror("sendto");
					free(DATA);
					exit(1);
				}

				pkt_num++;
				
				
				//printf("A_rcv snd now\n");
	
			}//end of send a bulk
			
			free(DATA);
			
			/*
			if(debug == 1){
				printf("No. %d bulk sent from A_rcv.\n", bulk_num);
				fflush(stdout);
			}
			*/
			
			bulk_num++;
			
			
		} else if (t==m+1) {			
			//[forward]  when B_rcv rcv enough bulks, it will send DATA[0] = 4 to A_rcv
			//[backward] when B_rcv rcv enough bulks, it will send DATA[0] = 7 to A_rcv
			
			info->cur_mode = m+2;
			//[forward]  cur_mode = 3 + 2 = 5
			//[backward] cur_mode = 6 + 2 = 8
			
			//printf("A_rcv finish.\n");
			break;
		} else {	// unexpected packets
			/*
			if(debug == 1){
				printf("A_rcv error pkt\n");
				fflush(stdout);
			}
			*/
		}
		
		//already sent all samples
		if(request_num >= max_n){
			info->cur_mode = m+2;
			break;
		}
		
		//printf("req_num: %d\n", request_num);

	}
	//printf("A_rcv finish.\n");
	//fflush(stdout);
	return 0;
}


//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
int B_send(int send_socket, struct shared *info, int m) {
int i;
int *DATA;
int numbytes;
struct timespec ts;
	ts.tv_sec = 0;
	i = 0;
	
extern int pksize_info;
extern int max_n;

struct timespec ts1;	//the snd time
struct timespec t_out;	//for time out test
double time_elapse=0;	//elapse time from send the request
double cur_time=0;		//current time
double start_time=0;	//time out timer start time

unsigned long temp_interval;
unsigned int big_sleep;

/*
//for date print
time_t  ticks;
struct tm *t;
char  date[20];

	ticks = time(NULL);
	t= localtime(&ticks);
	strftime(date,127,"%Y-%m-%d %H:%M:%S",t);
	//printf("%s\t",date);
	//fflush(stdout);
*/
	
	//constant sleep interval or fix-probing rate
	if(constant_interval!=0 || probing_rate!= 0){
		
		if(constant_interval!=0){
			temp_interval = constant_interval*1000000;		//millisec to nanosec
		}else if(probing_rate!= 0){
			temp_interval = ((pksize_info+HEADER)*8*bulk_len/probing_rate)*1000;   //in nanosec
		}
		
		ts.tv_sec = (int)(temp_interval/1000000000.0);
		ts.tv_nsec = (int)temp_interval%1000000000;
		
		big_sleep = (int)(temp_interval/1000000000.0);
		
		while(info->n < max_n){
			DATA = (int *)malloc(pksize_info);
			memset(DATA,'1',pksize_info);
			DATA[0] = m;
			DATA[1] = i;
			DATA[4] = info->request_num;	//send the num to A_recv
			
			clock_gettime(CLOCK_MONOTONIC, &ts1);
			info->cap_send[i%CAP_MAX] = ts1.tv_sec + ts1.tv_nsec/1000000000.0;
			
			info->cap_recv1[i%CAP_MAX] = -1;
			info->cap_recv2[i%CAP_MAX] = -1;
			
			if ((numbytes=sendto(send_socket, DATA, pksize_info, 0, (struct sockaddr *)&addr_f, sizeof(struct sockaddr))) == -1) {
				perror("sendto");
				free(DATA);
				exit(1);
			}
			free(DATA);
			i++;
			i %= CAP_MAX;
			
			/*
			if(debug==1){
				printf("No. %d request from B_send\n", info->request_num);
				fflush(stdout);
			}
			*/
			
			//nanosleep(&ts,NULL);
			
			
			if(big_sleep>=1){
				sleep(big_sleep);
				nanosleep(&ts,NULL);
			}else{
				nanosleep(&ts,NULL); // sleep for 10 microsec
			}
			
		}
		
	//auto-adjust sleep time
	}else{
	
		while(info->n < max_n){
		
			//wxlee: lock and wait the B_rcv rcv whole bulk
			if(info->waiting_flag == 1){	//need to wait the B_rcv to rcv a bulk
				
				//stat the timer for time out test
				clock_gettime(CLOCK_MONOTONIC, &t_out);
				cur_time = t_out.tv_sec + t_out.tv_nsec/1000000000.0;   //sec
				
				//start time
				if(start_time == 0){
					start_time = cur_time;
					continue;
				}
				
				time_elapse = cur_time - start_time;
				
				if(time_elapse >= (info->G/1000.0)*TIME_OUT_FACTOR){
					start_time = 0;
					info->waiting_flag = 0;		//resend the request pkt
					/*
					if(debug==1)
						printf("Request time out!\n");
					*/
				}
				continue;
			}
			
			
			ts.tv_nsec = 1000 * info->G; // microsec
			//printf("info->G = %3.3lf %ld\n",info->G, ts.tv_nsec);
			
			DATA = (int *)malloc(pksize_info);
			memset(DATA,'1',pksize_info);
			DATA[0] = m;
			DATA[1] = i;
			DATA[4] = info->request_num;	//send the num to A_recv
			
			clock_gettime(CLOCK_MONOTONIC, &ts1);
			info->cap_send[i%CAP_MAX] = ts1.tv_sec + ts1.tv_nsec/1000000000.0;
			
			info->cap_recv1[i%CAP_MAX] = -1;
			info->cap_recv2[i%CAP_MAX] = -1;
			if ((numbytes=sendto(send_socket, DATA, pksize_info, 0, (struct sockaddr *)&addr_f, sizeof(struct sockaddr))) == -1) {
				perror("sendto");
				free(DATA);
				exit(1);
			}
			free(DATA);
			i++;
			i %= CAP_MAX;
			
			nanosleep(&ts,NULL);
			
			/*
			if(debug==1){
				printf("No. %d request from B_send\n", info->request_num);
				fflush(stdout);
			}
			*/

			info->waiting_flag = 1;		//lock
			
		}//end while
	}//end else if
	//printf("B_snd finish.\n");
	return 0;
}


//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
int B_recv(int send_socket, int recv_socket, struct shared *info, int m) {
char buf[MAXBUFLEN];
int addr_len;
int numbytes;
int *DATA;
int i;
double disp, rtt1, rtt2, rttsum, C;
double t1,t2;
	info->cap_C = 0;
	info->cap_RTT_SUM = 10000000;

extern int pksize_info;
extern int max_n;
extern float utilization;
extern double *DISP_LOG;

struct timespec ts01, ts02;
struct timespec ts11, ts22;
int	bulk_num = 0;			//the num of bulk in B_rcv
int series_num = 0;			//the num of K in a bulk in B_rcv

int temp_bulk_num;			//the num of bulk from A_rcv
int cur_series_num;			//the num of K in bulk from A_rcv
int pkt_out_of_order = 0;			//the num of pkt loss
int pkt_out_of_order_flag = 0;		//to note the pkt loss or not in a bulk
long total_pkt_rcv = 0;
long pkt_loss = 0;
//int pre_num = -1;				//for pkt_loss detection
int pkt_num;
int cnt=0;					//for DISP_LOG

extern int disp_flag;


	addr_len = sizeof(struct sockaddr);
	
	clock_gettime(CLOCK_MONOTONIC, &ts01);
	t1 = ts01.tv_sec + ts01.tv_nsec/1000000000.0;
	disp_flag =0;
	
	while (1) {
	
		if ((numbytes=recvfrom(recv_socket, buf, MAXBUFLEN-1, 0, (struct sockaddr *)&addr_b, &addr_len)) == -1) {
			perror("recvfrom");
			exit(1);
		}
		if (numbytes != pksize_info) continue;
		DATA = buf;
		
		//wxlee
		temp_bulk_num = DATA[4];		//the bulk num from A_recv
		cur_series_num = DATA[5];		//the series num in K from A_recv
		pkt_num = DATA[6];				//the series num of pkt
		
		//printf("B_rcv bulk.pkt num: %d.%d.%d\n", temp_bulk_num, cur_series_num, series_num);
		//fflush(stdout);
		
		if(cur_series_num > bulk_len || temp_bulk_num > max_n)
			continue;
		
		
		if (DATA[0]!=m) { continue; }
		
		total_pkt_rcv++;				//the total pkt rcv at B_rcv
		
		
		i = DATA[1] % CAP_MAX;					//cyclic
		
		if (DATA[3]==0){						//rcv first pkt of bulk
			info->cap_k[i] = DATA[2];			//save the bulk length
			info->cap_last_k[i] = 0;			
			clock_gettime(CLOCK_MONOTONIC, &ts11);
			info->cap_recv1[i] = ts11.tv_sec + ts11.tv_nsec/1000000000.0;
		
			
		} else if (DATA[3]==info->cap_last_k[i]+1){		//the seriers num of pkt in a bulk
			info->cap_last_k[i] = DATA[3];				//check the num of bulk, continuous or not.
		
			
			//rcv all pkt of a bulk
			if (DATA[3] == info->cap_k[i]){				//rcv last pkt of a bulk

				clock_gettime(CLOCK_MONOTONIC, &ts22);
				info->cap_recv2[i] = ts22.tv_sec+ ts22.tv_nsec/1000000000.0;

				// CapProbe!!!!!
				rtt1 = info->cap_recv1[i] - info->cap_send[i];
				rtt2 = info->cap_recv2[i] - info->cap_send[i];
				disp = rtt2 - rtt1;
				rttsum = rtt1 + rtt2;
				
				
				info->request_num += 1;		//increase the request num
				info->waiting_flag = 0;		//unlock, B_snd can request a new bulk
				//printf("B_rcv unlock\n");
				
				//the disp is lower than 1 ms
				//if(disp <= DISP_THRESH) disp_flag = TRUE;
				
				//printf("snd/rcv1/rcv2: %.6f   %.6f   %.6f\n", info->cap_send[i%CAP_MAX], info->cap_recv1[i], info->cap_recv2[i]);
				//printf("rttsum: %.6f; rtt1: %.6f; rtt2: %.6f\n", rttsum, rtt1, rtt2);
				
				if (disp>0 && disp<100){
					info->n = info->n + 1;
					C = (pksize_info + HEADER) * 8 * bulk_len / disp / (double) 1000000;
					if (rttsum < info->cap_RTT_SUM) {
						info->G = disp * 1000 * 2 / utilization;   // ms
						info->cap_RTT_SUM = rttsum;
						info->cap_C = C;
						
						//the disp is lower than 1 ms
						//if(disp <= DISP_THRESH) disp_flag = TRUE;
					}
					
					//store disp to DISP_LOG
					DISP_LOG[cnt] = disp;
					cnt++;
					
					if(verbose==1){
						printf("pkt_num: %d  series_num:%d  C= %3.6lf   cap_C= %3.6lf  disp= %3.6lf\n", pkt_num, info->n, C, info->cap_C, disp);
						fflush(stdout);
					}
					
				}
				
			}
			
			//rcv enough bulks, finish THIS direction
			if (info->n >= max_n) {
				info->cur_mode = m+2;

				DATA = (int *)malloc(pksize_info);
				memset(DATA,'1',pksize_info);
									
				//[forward]  DATA[0] = m+1 = 4, send it to A_rcv and finish.
				//[backward] DATA[0] = m+1 = 7, send it to A_rcv and finish.
				DATA[0] = m+1;
				
				if ((numbytes=sendto(send_socket, DATA, pksize_info, 0, (struct sockaddr *)&addr_f, sizeof(struct sockaddr))) == -1) {
					perror("sendto");
					free(DATA);
					exit(1);
				}
				free(DATA);
				//printf("send finish-1\n");
				break;
			}
			
		}else{		
		
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &ts02);
	t2 = ts02.tv_sec + ts02.tv_nsec/1000000000.0;

	
	if(verbose == 1) {
		if(disp_flag == 1){
			printf("The dispersion is lower than 10 ms, please increase '-k'.\n");
			//printf("Now k is %ld. You can use '-k %ld' and run it again.\n", bulk_len, bulk_len*10);
			fflush(stdout);
		}
		
		//printf("Link Capacity // Packet Loss // Packet Recv // Packet Loss Rate // Time\n");
		fflush(stdout);
	}
	
	
	//printf("Final: %3.5lf\t%ld\t%ld\t%3.2lf\t%3.5lf\n", info->cap_C, pkt_num-total_pkt_rcv , total_pkt_rcv, ((double)(pkt_num-total_pkt_rcv)/(double)pkt_num)*100, t2 - t1);
	//printf("%3.5lf\t%ld\t%ld\t%3.2lf\t%3.5lf\n", info->cap_C, pkt_num-total_pkt_rcv , total_pkt_rcv, ((double)(pkt_num-total_pkt_rcv)/(double)pkt_num)*100, t2 - t1);
	
	
	local_result.LinkCap = info->cap_C;
	local_result.PktLoss = pkt_num-total_pkt_rcv;	
	local_result.PktRecv = total_pkt_rcv;
	local_result.PktLossRate = ((double)(pkt_num-total_pkt_rcv)/(double)pkt_num)*100;
	local_result.ElapsedTime = t2 - t1;
	
	RESULT[0] = info->cap_C;					//Link Cap
	RESULT[1] = pkt_num-total_pkt_rcv;			//pkt loss
	RESULT[2] = total_pkt_rcv;					//pkt rcv
	RESULT[3] = ((double)(pkt_num-total_pkt_rcv)/(double)pkt_num)*100; //pkt loss rate
	RESULT[4] = t2 - t1;						//time
	
	//printf("%f\t%f\t%f\t%f\t%f\n", RESULT[0], RESULT[1], RESULT[2], RESULT[3], RESULT[4]);
	
	//printf("B_rcv finish.\n");
	//fflush(stdout);
	
	return 0;
}
