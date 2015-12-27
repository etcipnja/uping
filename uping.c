//-----------------------------------------------------------------------------
//UPNING v1.1 by ET
//uping.x allows to test UDP connectivity between hosts 
//
//compilation: gcc uping.c -lrt -o uping.x
//
//run as a server (optional): ./uping.x -d -p <port_to_listen>
//	you can use standard echo server instead
//run as a client : uping.x <host> -p <port> -i <interval> -s <size> -c <count> -q
//	<host> -p <port> - where to send UDP packets to
//	-i <interval> - interval in sec (-i.1 - send every 100ms)
//	-s <size> - packet size in bytes
//	-q - quiet mode    
//you can send SIGUSR1 to get the statistic for the last interval 


#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/uio.h>
#include <getopt.h>
#include <assert.h>
#include <sys/types.h>
#include <time.h>
#include <inttypes.h>
#include <signal.h>

//-----------------------------------------------------------------------------
struct
{
	int 		fd;
	int 		magic;
	sigset_t 	orig_mask;
	uint64_t 	last_ts;
	struct packet * packets;
	int 		window_size;
	int 		window_cursor;
	int 		stop_signal;
	unsigned 	packets_sent;
	
} var;

//-----------------------------------------------------------------------------
struct
{
	int 		max_diff;
        int 		min_diff;
        int 		avg_diff;
	unsigned 	received;
	unsigned 	sent;
        uint64_t start;
} stat;

//-----------------------------------------------------------------------------
struct packet
{
	unsigned 	sequence;
	uint64_t 	sent_ts;
};

//-----------------------------------------------------------------------------
struct 
{
	char * 		host;
	unsigned int 	ip;
	float 		interval;
	unsigned 	size;
	uint16_t 	port;
	unsigned 	count;
	int 		silent;
	int 		daemon;
} arguments;

//-----------------------------------------------------------------------------
uint64_t now()
{
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (ts.tv_sec*10000+ts.tv_nsec/100000);
}

//-----------------------------------------------------------------------------
void print_stat()
{
	uint64_t n=now();
	int i=0;
       	for (;i<var.window_size;i++)
	{
		//we are not going to wait for the pending packets
        	if (var.packets[i].sent_ts && n-var.packets[i].sent_ts<15000)
			stat.sent--;
	}
	int worked=now()-stat.start;
        printf("\n--- %s:%i uping statistic ---\n",arguments.host,arguments.port);
        fprintf(stderr,"%i packets transmitted, %i received, %i%% packet loss, time %.1fms\n",
		stat.sent,stat.received,stat.sent!=0?(stat.sent-stat.received)*100/stat.sent:0,worked/10.0);
        fprintf(stderr,"rtt min/avg/max = %.1f/%.1f/%.1f\n\n",stat.min_diff/10.0,
		stat.avg_diff/10.0,stat.max_diff/10.0);
	
	//reset stat
	memset(&stat,0,sizeof(stat));
        stat.start=now();
	var.stop_signal=0;
	memset(var.packets,0,sizeof(struct packet)*var.window_size);
}

//-----------------------------------------------------------------------------
void sig_handler(int signo)
{
  	if (signo == SIGINT) var.stop_signal=1;
	if (signo == SIGUSR1) var.stop_signal=2;
}		

//-----------------------------------------------------------------------------
void init(int argc, char* argv[])
{
	arguments.host=NULL;
	arguments.interval=1.0;
	arguments.size=250;
	arguments.port=7;
	arguments.count=0;
	arguments.silent=0;
	arguments.daemon=0;
	
	memset(&var,0,sizeof(var));
	var.magic=random();
	var.fd=-1;
	
	memset(&stat,0,sizeof(stat));
	stat.start=now();

        int c=0;
	int stop=0;
        while (stop!=1 && optind < argc)
        {
                c = getopt (argc, argv, "+s:i:p:c:qd");
                
		switch(c)
		{
		case -1:
			if (arguments.host==NULL) 
			{
				arguments.host=argv[optind++];
				struct hostent * pxHostEnt=gethostbyname(arguments.host);
        			if (pxHostEnt==NULL || pxHostEnt->h_length!=sizeof(uint32_t))
        			{
                			printf("failed to resolve remote socket address (err=%d)\n",strerror(errno));
                			exit(1);
        			}
				memcpy((void *)&arguments.ip,(void *)pxHostEnt->h_addr,sizeof(uint32_t));
			}
			else stop=1;
			break;
                case 's':
			arguments.size=atoi(optarg);
			if (arguments.size<2*sizeof(int) || arguments.size>1024) stop=1;
                        break;
		case 'c':
                        arguments.count=atoi(optarg);
                        if (arguments.count<=0) stop=1;
			break;
                case 'i':
                        arguments.interval=atof(optarg);
                        if (arguments.interval<=0.0001) stop=1;
			break;
		case 'p':
                        arguments.port=atoi(optarg);
                        break;
		case 'q':
			arguments.silent=1;
			break;
		case 'd':
			arguments.daemon=1;
			break;
		case '?':
			stop=1; 
			break;
                default: 
			assert(0);
                }
        }
        if ((arguments.host==NULL && arguments.daemon==0) || stop==1) 
	{
		printf("Usage: uping.x <host> -p <port> -i <interval> -s <size> -c <count> -q \n");
		printf(" OR    uping.x -d  -p <port> \n");
		exit(1);
	}

	//create window for return packets
	var.window_size=2/arguments.interval+1; //2 sec 
	var.packets=malloc(var.window_size*sizeof(struct packet));
	memset(var.packets,0,sizeof(struct packet)*var.window_size);

	//init socket
      	var.fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if (var.fd==0)
        {
                printf("can't create socket %s\n",strerror(errno));
                exit(2);
        }
        struct sockaddr_in xLocalAddr;
        memset((char*) &xLocalAddr,0, sizeof(xLocalAddr));
        xLocalAddr.sin_family=AF_INET;
        if (arguments.daemon) xLocalAddr.sin_port=arguments.daemon?htons(arguments.port):0;

        if (bind(var.fd,(struct sockaddr*)&xLocalAddr,sizeof(xLocalAddr))==-1)
        {
                printf("can't bind socket %s\n",strerror(errno));
                exit(2);
        }

}	

//-----------------------------------------------------------------------------
void send_packet()
{
	char message[arguments.size];

	var.window_cursor++;
	if (var.window_cursor==var.window_size) var.window_cursor=0;
	memset(var.packets+var.window_cursor,0,sizeof(struct packet));
	stat.sent++;
	var.last_ts=now();

	var.packets[var.window_cursor].sent_ts=var.last_ts;
	var.packets[var.window_cursor].sequence=++var.packets_sent;
	
	memcpy(&message[0*sizeof(int)],&var.magic,sizeof(int));
	memcpy(&message[1*sizeof(int)],&var.packets[var.window_cursor].sequence,sizeof(int));
	
	struct sockaddr_in dest;
        dest.sin_family=AF_INET;
        dest.sin_port= htons(arguments.port);
        dest.sin_addr.s_addr=arguments.ip;

        if (sendto(var.fd,message, sizeof(message) ,0, (struct sockaddr *)&dest, sizeof(dest))==-1)
                printf("%s\n",strerror(errno));
}
//-----------------------------------------------------------------------------
struct timespec get_timeout()
{
	uint64_t n=now();
	struct timespec tv;
	int next=0;
	
	//calculate time needed to send next packet
	if (var.packets_sent<arguments.count || arguments.count==0)
	{
		next=arguments.interval*10000-(n-var.last_ts);
		if (next<0) next=0;
	} 
	else //no next packet - lets wait for pending packets
	{
		uint64_t max_pending=0;
		int i=0;
        	for (;i<var.window_size;i++)
        	{
                	if (var.packets[i].sent_ts!=0 && var.packets[i].sent_ts>max_pending)
                        	max_pending=var.packets[i].sent_ts;

        	}
        	if (max_pending!=0) //something is pending
        	{
			next=n-max_pending;
                	if (next>20000) next=-1; //too old
                	else next=20000-next;
        	}
	}

	tv.tv_sec = next/10000;
        tv.tv_nsec = (next%10000)*100000;
	

	//printf("W %i,%i, %i\n",tv.tv_sec,tv.tv_nsec, next);
	return tv;
}

//-----------------------------------------------------------------------------
void do_server()
{
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(var.fd, &rfds);

        int retval = pselect(var.fd+1, &rfds, NULL, &rfds, NULL, &var.orig_mask);
        if (retval == -1) //error
        {
                if (errno!=EINTR)
                {
                        printf("select failed %s\n",strerror(errno));
                        exit(2);
                }
        }
        else if (retval) 
        {
		struct sockaddr dest;
		socklen_t dest_size = sizeof(dest);
                char buffer[1024];
		int size=recvfrom(var.fd, &buffer[0], sizeof(buffer),0, &dest, &dest_size);
                if (-1==size)  printf("recvfrom failed %s\n",strerror(errno));
		else	
		{
        		if (sendto(var.fd, buffer, size ,0, (const struct sockaddr *)&dest, dest_size)==-1)
        			printf("%s\n",strerror(errno));
        	}
	}
}

//-----------------------------------------------------------------------------
int do_client()
{
	fd_set rfds;
	const struct timespec tv=get_timeout();
    	FD_ZERO(&rfds);
    	FD_SET(var.fd, &rfds);

    	int retval = pselect(var.fd+1, &rfds, NULL, &rfds, &tv, &var.orig_mask);
    	if (retval == -1) //error
	{
		 if (errno==EINTR)
                 {
			if (var.stop_signal==1) return 0;
	                if (var.stop_signal==2)
                        {
                        	print_stat();
                                return 1;
                        }
                }
		else
       		{	
			printf("select failed %s\n",strerror(errno));
			exit(2);
		}
	}
    	else if (retval) //reply received
	{
		char buffer[arguments.size];
		int size=recv(var.fd, &buffer[0],sizeof(buffer),0);
		if (var.stop_signal==1) return 0;
		if (-1==size)
		{
			printf("recv failed %s\n",strerror(errno));
                       	exit(2);
		}
		int m;
		memcpy(&m, &buffer[0*sizeof(int)],sizeof(int));
		if (m==var.magic)
		{
			int sequence;
			memcpy(&sequence, &buffer[1*sizeof(int)],sizeof(int));
			
			int i=0;
			for (;i<var.window_size;i++)
				if (var.packets[i].sequence==sequence) break;
			if (i!=var.window_size)
			{
				stat.received++;
				int diff=now()-var.packets[i].sent_ts;
				if (diff<stat.min_diff || stat.min_diff==0) stat.min_diff=diff;
				if (diff>stat.max_diff || stat.max_diff==0) stat.max_diff=diff;
				if (stat.avg_diff==0) stat.avg_diff=diff;
				else { stat.avg_diff+=diff; stat.avg_diff/=2; }
				if (!arguments.silent)
				{ 
					printf("%i bytes from %s:%i sequence %i, time %.1f ms\n",
						size,arguments.host,arguments.port,var.packets[i].sequence, diff/10.0);
				}
				memset(var.packets+i,0,sizeof(struct packet));
			}
		}
		else printf("wrong magic\n");
	}
	else //timeout
	{
		if (var.packets_sent>=arguments.count && arguments.count!=0)	return 0;
		send_packet();
	}

	return 1; 

}
//-----------------------------------------------------------------------------
int main(int argc, char * argv[])
{
	if (signal(SIGINT, sig_handler) == SIG_ERR)
	{
 		printf("can't catch SIGINT\n");
		exit(2);
	}
	if (signal(SIGUSR1, sig_handler) == SIG_ERR)
        {
                printf("can't catch SIGUSR1\n");
                exit(2);
        }
	
	sigset_t mask;
	sigemptyset (&mask);
	sigaddset (&mask, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &mask, &var.orig_mask) < 0) 
	{
		printf("can't mask SIGUSR1\n");
		exit(2);
	}

	init(argc,argv);
	
	if (arguments.daemon)
	{
		printf("uping listening on port %i\n", arguments.port);
		while(var.stop_signal!=1)
		{	
			do_server();
		}
	}
	else
	{
		printf("UPING %s:%i, %i bytes of data, interval %f, count %i\n",arguments.host,
			arguments.port,arguments.size,arguments.interval, arguments.count);
		int i=0;
		for (; var.stop_signal!=1; i++)
		{
			if (!do_client()) break;
		}
	
		print_stat();
	}
	return 0;
}

