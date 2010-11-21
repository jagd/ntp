#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h> /* gettimeofday() */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <time.h> /* for time() and ctime() */

#define UTC_NTP 2208988800U /* 1970 - 1900 */

/* get Timestamp for NTP in LOCAL ENDIAN */
void gettime64(uint32_t ts[])
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	ts[0] = tv.tv_sec + UTC_NTP;
	ts[1] = (4294*(tv.tv_usec)) + ((1981*(tv.tv_usec))>>11);
}


int die(const char *msg)
{
	if (msg) {
		fputs(msg, stderr);
	}
	exit(-1);
}


void log_request_arrive(uint32_t *ntp_time)
{
	time_t t; 

	if (ntp_time) {
		t = *ntp_time - UTC_NTP;
	} else {
		t = time(NULL);
	}
	printf("A request comes at: %s", ctime(&t));
}


void log_ntp_event(char *msg)
{
	puts(msg);
}


int ntp_reply(
	int socket_fd,
	struct sockaddr *saddr_p,
	socklen_t saddrlen,
	unsigned char recv_buf[],
	uint32_t recv_time[])
{
	/* Assume that recv_time is in local endian ! */
	unsigned char send_buf[48];
	uint32_t *u32p;

 	/* do not use 0xC7 because the LI can be `unsynchronized` */
	if ((recv_buf[0] & 0x07/*0xC7*/) != 0x3) {
		/* LI VN Mode stimmt nicht */
		log_ntp_event("Invalid request: found error at the first byte");
		return 1;
	}

	/* füllt LI VN Mode aus
	   	LI   = 0
		VN   = Version Nummer aus dem Client
		Mode = 4
	 */
	send_buf[0] = (recv_buf[0] & 0x38) + 4;

	/* Stratum = 1 (primary reference)
	   Reference ID = 'LOCL"

	       (falscher) Bezug auf der lokalen Uhr.
	 */
	/* Stratum */
	send_buf[1] = 0x01;
	/* Reference ID = "LOCL" */
	*(uint32_t*)&send_buf[12] = htonl(0x4C4F434C);

	/* Copy Poll */
	send_buf[2] = recv_buf[2];

	/* Precision in Microsecond ( from API gettimeofday() ) */
	send_buf[3] = (signed char)(-6);  /* 2^(-6) sec */

	/* danach sind alle Werte DWORD aligned  */
	u32p = (uint32_t *)&send_buf[4];
	/* zur Vereinfachung , Root Delay = 0, Root Dispersion = 0 */
	*u32p++ = 0;
	*u32p++ = 0;

	/* Reference ID ist vorher eingetragen */
	u32p++;

	/* falscher Reference TimeStamp,
	 * wird immer vor eine minute synchronisiert,
	 * damit die Überprüfung in Client zu belügen */
	gettime64(u32p);
	*u32p = htonl(*u32p - 60);   /* -1 Min.*/
	u32p++;
	*u32p = htonl(*u32p);   /* -1 Min.*/
	u32p++;

	/* Originate Time = Transmit Time @ Client */
	*u32p++ = *(uint32_t *)&recv_buf[40];
	*u32p++ = *(uint32_t *)&recv_buf[44];

	/* Receive Time @ Server */
	*u32p++ = htonl(recv_time[0]);
	*u32p++ = htonl(recv_time[1]);

	/* zum Schluss: Transmit Time*/
	gettime64(u32p);
	*u32p = htonl(*u32p);   /* -1 Min.*/
	u32p++;
	*u32p = htonl(*u32p);   /* -1 Min.*/

	if ( sendto( socket_fd,
		     send_buf,
		     sizeof(send_buf), 0,
		     saddr_p, saddrlen)
	     < 48) {
		perror("sendto error");
		return 1;
	}

	return 0;
}


void request_process_loop(int fd)
{
	struct sockaddr src_addr;
	socklen_t src_addrlen = sizeof(src_addr);
	unsigned char buf[48];
	uint32_t recv_time[2];
	pid_t pid;

	while (1) {
		while (recvfrom(fd, buf,
				48, 0,
				&src_addr,
				&src_addrlen)
			< 48 );  /* invalid request */

		gettime64(recv_time);
		/* recv_time in local endian */
		log_request_arrive(recv_time);

		pid = fork();
		if (pid == 0) {
			/* Child */
			ntp_reply(fd, &src_addr , src_addrlen, buf, recv_time);
			exit(0);
		} else if (pid == -1) {
			perror("fork() error");
			die(NULL);
		}
		/* return to parent */
	}
}


void ntp_server()
{
	int s;
	struct sockaddr_in sinaddr;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1) {
		perror("Can not create socket.");
		die(NULL);
	}

	memset(&sinaddr, 0, sizeof(sinaddr));
	sinaddr.sin_family = AF_INET;
	sinaddr.sin_port = htons(123);
	sinaddr.sin_addr.s_addr = INADDR_ANY;

	if (0 != bind(s, (struct sockaddr *)&sinaddr, sizeof(sinaddr))) {
		perror("Bind error");
		die(NULL);
	}

	log_ntp_event(	"\n========================================\n"
			"= Server started, waiting for requests =\n"
			"========================================\n");

	request_process_loop(s);
	close(s);
}


void wait_wrapper()
{
	int s;
	wait(&s);
}


int main(int argc, char *argv[], char **env)
{
	signal(SIGCHLD,wait_wrapper);
	ntp_server();
	/* nicht erreichbar: */
	return 0;
}
