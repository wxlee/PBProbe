.PHONY: clean

CC = gcc
CFLAGS = -w  #ignore all error
LINK = -lrt -lm
SRC_SERVER = pbprobe_server.c
SRC_CLIENT = pbprobe_client.c
OTHER = run_exp.c control_fun.c
NAME_SERVER = pbprobe_server
NAME_CLIENT = pbprobe_client

all: pbprobe_server pbprobe_client

pbprobe_server: 
	@-${CC} ${CFLAGS} ${SRC_SERVER} ${OTHER} -o ${NAME_SERVER} ${LINK}

pbprobe_client: 
	@-${CC} ${CFLAGS} ${SRC_CLIENT} ${OTHER} -o ${NAME_CLIENT} ${LINK}
	
clean:
	@rm -f *.o

	
#gcc pbprobe_server.c run_exp.c set_parameter.c -o pbprobe_server -lrt
#gcc pbprobe_client.c run_exp.c set_parameter.c -o pbprobe_client -lrt