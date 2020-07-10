#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>

#define MAXBUF 8192
#define BLACK_LIST_MAX 10
#define GET "GET"
#define LISTENQ 100
#define HTTPPORT "80"
#define IP_CACHE_SIZE 10
#define P_CACHE_SIZE 15
#define content_len "Content-Length"
#define IP_SIZE 16
#define HOSTNAME_MAX 253

pthread_mutex_t ip_lock;

/*
	struct http_header
	------------------
		-organizes important data for parse http_header
		-line is first line of http request 
*/

struct http_header{
	
	char* line;
	char* method;
	char* url;
	char* portcheck;
	char* host;
	char* http_ver;

};


/*
	struct IPCache
	--------------
		-struct to handle data for ip caching functions
		-caches hostname and ip to buffer
*/

struct IPCache{

	char* ip;
	char* hostname;
};

/*
	struct thread_arg
	-----------------
		-threads must share resources
		-pass cache buffers pointers
		-size of cache buffers
		-accepted connection

*/

struct thread_arg{
	
	const struct IPCache* ips;
	int* ip_cache_size;
	int fd;
	int ttl;	
};


/*
	void handle_request(struct* thread_arg conn)
	------------------
		-worker function for thread
		-given input argument of struct thread_arg to pass cache and socket descriptor
		-no return value
*/

void handle_request(struct thread_arg* args);

/*
	int serverSocket(int port)
	--------------------------
		-function to create a server socket type state
		-performs socket, setsockopt, bind, listen
		-port is the stdin requested port
		-return of socket descriptor on success
		-return of -1 on error
*/

int  serverSocket(int port);



/*
	int error400(int fd);
	---------------------
		-function to send error message to client
		-input of socket descriptor
		-return -1 on failure
		-return bytes sent on success
*/

int error400(int fd);


/*
	char* dnslookup(char* hostname)
	-------------------------------
		-function too lookup Ip of hostname
		-uses gethostbyname 
		-returns pointer to ip_addr
		WARNING<must free() the return value if not NULL>
		-returns NULL on failure
*/

char* dnslookup(char* hostname);


/*

	int send_to_server(int fd, const struct http_header *h)
	-------------------------------------------------------
		-function to send http_header from client to server
		-takes socket to server as int fd
		-takes http_header struct pointer
		-sends proper format to the server with sprintf
		-return value of -1 upon error otherwise number of bits written

*/

int send_to_server(int fd, const struct http_header* h);

/*
	int cache_IP(const struct IPCache *ip_cache, int *size, char* hostname)
	------------------------------------------------------------------------
		-function to check and cache ip_address and hostname to ip_cache
		-takes in IPCache buffer as *ip_cache, size of cache buffer int size, and hostname
		-if ip not in cache alread then increment buffer in ciricular array and add entry
		-returns -1 if ip_addr not found from dnslookup
		-returns index to entry in *ip_cache;

*/

int cache_IP(const struct IPCache *ip_cache, int *size, char* hostname);

/*
	int check_blk_list(char* host)
	------------------------------
		-function to check blacklist.txt for banned hostnames
		-opens blacklist.txt and checks against hostname
		-returns -1 if ip in blacklist.txt
		-returns 0 if not

*/

int check_blk_list(char* host);


/*
	int page_cache(char* url, int socket_fd, int ttl)
	-------------------------------------------------
		-function to check for page in cache
		-takes in char* url to check against, int socket_fd to client, int ttl for expire cache time
		-if file is found and has not expired then send to client return 0 
		-if not found or expire then return -1;

*/

int page_cache(char* url, int socket_fd, int ttl);


/*
	FILE* create_cache_page(char* url)
	-----------------------------------
		-creates a file handle to cache page for writing
		-takes char* url as argument from requested page
		-return NULL on failure
		-return FILE* to open file on success

*/

FILE* create_cache_page(char* url);


/*
	void *thread(void *vargp)
	--------------------------
	-function to pass to thread, handles thread_arg
	-call handle_request
	-responsible for closing connection and freeing thread_arg

*/

void *thread(void *vargp){
	

	struct thread_arg* args = (struct thread_arg*)vargp;
	pthread_detach(pthread_self());
	handle_request(args);
	close(args->fd);
	free(args);
	return NULL;

}


/*

	int replace(char* str, char delim, char rep)
	--------------------------------------------
		-function to replace char in a string
		-takes in char* str as string to change, char delim as char to change, rep, as replacement string
		-returns 1 on success

*/

int replace(char* str, char delim, char rep){

	char* temp = str;

	while( (temp != NULL) && (*temp != '\0')){
		
		if(*temp == delim){
			
			*temp = rep;
		} 
		temp++;

	}
	return 1;

}


int main(int argc, char* argv[])
{
	int port, ttl;
	int clientfd;
	int *i_cache_size;
	int listenfd;
	int client_s = sizeof(struct sockaddr_in);
	struct sockaddr_in clientaddr;
	struct IPCache G_IP_Cache[IP_CACHE_SIZE];	//ip_cache buffer
	struct thread_arg *t_args;
	pthread_t tid;

	if(argc != 3){

		fprintf(stderr, "usage: %s <port> <ttl>\n" , argv[0]);
		exit(0);

	}

	port = atoi(argv[1]);
	ttl = atoi(argv[2]);

	//allocate cache size for ip buffer and page buffer	
	i_cache_size = malloc(sizeof(int));
		

	//zero out cache buffer
	bzero((void*)&G_IP_Cache, sizeof(G_IP_Cache) * IP_CACHE_SIZE);

	//initialize the ip cache buffer	
	for(int i = 0; i < IP_CACHE_SIZE; i++){

		G_IP_Cache[i].ip = malloc(IP_SIZE);
		G_IP_Cache[i].hostname = malloc(HOSTNAME_MAX);
	}

	//set the sizes of buffers to 0	
	*i_cache_size = 0;

	//initialize mutex lock for threads
	pthread_mutex_init(&ip_lock, NULL);
	
	//setup proxy socket to client
	listenfd = serverSocket(port);

	if(listenfd < 0){
		exit(0);
	}


	while(1){

		//create args to pass cache buffers and incoming connection
	  
		clientfd = accept(listenfd, (struct sockaddr*)&clientaddr, &client_s);

		t_args = malloc(sizeof(struct thread_arg));
		bzero((void*)t_args, sizeof(struct thread_arg));
		t_args->fd = clientfd;
		t_args->ttl = ttl;

		t_args->ips = G_IP_Cache;
		t_args->ip_cache_size = i_cache_size;
		
		
		pthread_create(&tid, NULL, thread, t_args); 

	}

	//lock the mutext so no hanging threads are using it
	pthread_mutex_lock(&ip_lock);
	
	//free the ip size 
	free(i_cache_size);

	//release all malloced hostname and ip spaces
	for(int i = 0; i < IP_CACHE_SIZE; i++){

		free(G_IP_Cache[i].ip);
		free(G_IP_Cache[i].hostname);
	}
	
	//unlock and destroy the lock
	pthread_mutex_unlock(&ip_lock);
	pthread_mutex_destroy(&ip_lock);
	

	return 0;

}

int check_blk_list(char* host){

	FILE* fptr;
	char filename[] = "blacklist.txt";
	char line[253];

	fptr = fopen(filename, "r");
	if(fptr < 0){

		exit(1);
	}

	while( fgets(line , sizeof(line), fptr) != NULL){

		if((strncmp(host, line, strlen(host))) == 0){

			return -1;
		}

	}

	fclose(fptr);
	return 0;

	
}



int error400(int fd){

	int i;
	char error[] = "400 Bad Request\r\n\r\n";
	
	//write to socket
	i = write(fd, error, strlen(error));

	if(i < 0)
		return -1;
	
	return i;
}

int send_to_server(int fd, const struct http_header* h){
	
	//http header format for firefox

	char header_format[] = "%s http:%s %s\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:69.0) Gecko/20100101 Firefox/69.0\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nAccept-Language: en-US,en;q=0.5\r\nAccept-Encoding: gzip, deflate\r\nConnection: keep-alive\r\n\r\n";

	//max header limit for apache
	char* return_message = malloc(MAXBUF);

	//print to return message with format and http_header
	sprintf(return_message, header_format, h->method, h->url, h->http_ver, h->host);
	//write to socket
	int n  = write(fd, return_message, strlen(return_message));
	
	if( n < 0){
		//err handler
		printf("Failed to write to  %s\n", h->host);
		return -1;

	}

	printf("Forwarded Message to host %s \n%s",h->host, return_message );
		
	//cleanup
	free(return_message);	

	return n;
	

}

int cache_IP(const struct IPCache* ip_cache, int* c_size, char* hostname){

	int size, k;
	char* ip_addr;
	
	
	//err handle
	if(hostname == NULL){
		return -1;
	}
	
	if(*c_size < IP_CACHE_SIZE){ //check for size of cache
		
		size = *c_size;	//if small than max put regular size
	}
	else{
		size = IP_CACHE_SIZE; //if bigger then set to max

	}

	for(int i = 0; i < size; i++){

		//compare strings
 		k = strncmp(hostname, ip_cache[i].hostname, strlen(hostname));
		if(k == 0){
			
			printf("Cache Hit: %s\n", hostname); //if hostname match then return index
			return i;
		}
		

	}
	
	//if not hit in cache then look it up

	ip_addr = dnslookup(hostname);

	//err handle
	if(ip_addr == NULL){

		return -1;
	}
	
	//increment entries
	*c_size = *c_size + 1;

	//implement circular array
	if(*c_size >= IP_CACHE_SIZE){

		k = *c_size%IP_CACHE_SIZE;
			

	}else{
		
		k = *c_size;

	}

	//if buffer update, print size
	printf("buffer size == : %d\n", k );
	
	strcpy(ip_cache[k].hostname, hostname);
	strcpy(ip_cache[k].ip, ip_addr);
	//cleanup
	free(ip_addr);

	return k;


}


int page_cache(char* url, int socket_fd, int ttl){

	FILE* fptr;
	struct stat attr;
	time_t t;
	time_t now;
	size_t n;
	long fsize;

	char* buffer;
	char format[] = "%s";	
	char* file_name = malloc(strlen(url) + 1);
	
	sprintf(file_name, format, url);
	file_name[strlen(url)] = '\0';
	replace(file_name, '/', '#');

	fptr = fopen(file_name, "rb");
	
	if(fptr == NULL){
		
		printf("Cache Page Miss: %s\n", file_name);
		free(file_name);
		return -1;

	}

	stat(file_name, &attr);

	t = attr.st_mtime;
	now = time(NULL);
	t = t + ttl;


	if( t <= now){
		
		printf("Cache Page Expire: %s\n", file_name); 
		free(file_name);
		return -1;
	}
	
	printf("Cache Page Hit: %s\n", url);

	fseek(fptr, 0, SEEK_END);
	fsize = ftell(fptr);
	fseek(fptr, 0, SEEK_SET);

	//create buffer to fill file
	buffer = malloc(fsize);
	
	//read size of file into buffer
	fread(buffer, fsize, 1, fptr);
	write(socket_fd, buffer, fsize);

	printf("Sent %lu Bytes to Client \n\n", fsize);
	

	//close file pointer
	//clean up memory

	free(buffer);
	free(file_name);
	fclose(fptr);


	return 0;

}

FILE* create_cache_page(char* url){

	FILE* fptr;
	char format[] = "%s";
	char* file_name = malloc(strlen(url) + 1);
	
	//print to new buffer to avoid collision
	sprintf( file_name , format, url);
	file_name[strlen(url)] = '\0';

	//replace chars for file cache
	replace(file_name, '/', '#');

	fptr = fopen(file_name, "wb+");

	if(fptr == NULL){
		return NULL;
	}
	printf("Creating Cache Page: %s\n", file_name);
	
	//cleanup
	free(file_name);
	return fptr;

}


void handle_request(struct thread_arg* args ){

	int* ip_c_size;
	char* buf, *http_head;
	char *save_ptr1, *save_ptr2, *save_ptr3, *save_ptr4;	//save pointers for threadsafe strtok_r
	const struct IPCache *ip_cache = args->ips;		//get local pointer to ip_cache	
	struct http_header request;				//struct for http_header
	struct sockaddr_in server;				//struct for server connection
	FILE* cache_file;					//ptr to page caching
	

	int new_fd, i = 1, k, p_value;
	int connfd = args->fd;
	ip_c_size = args->ip_cache_size;
	
	//zero out all of our buffers
	buf = malloc(MAXBUF);
	bzero((void*)&server, sizeof(struct sockaddr_in));
	bzero((void*)&request, sizeof(struct http_header));
	bzero((void*)buf, MAXBUF);

	//set pointers for strtok_r to NULL
	save_ptr1 = NULL;
	save_ptr2 = NULL;
	save_ptr3 = NULL;
	save_ptr4 = NULL;

	read(connfd, buf, MAXBUF);
	
	printf("%s\n", buf);

	
	//parse data		
	request.line = strtok_r(buf, "\r\n", &save_ptr1 );
	if(request.line == NULL){

		error400(connfd);
		free(buf);
		return;


	}
	request.host = strtok_r(NULL, "\r\n", &save_ptr1);
	strtok_r(request.host, " ", &save_ptr2);
	request.host = strtok_r(NULL, " ", &save_ptr2);
		
	request.method = strtok_r(request.line, " ", &save_ptr3);

	//if not GET request send 400
	if((strncmp(request.method, GET, 3)) == 0){
		
		//more data parsing
		request.url = strtok_r(NULL, " ", &save_ptr3);
		request.http_ver = strtok_r(NULL, " ", &save_ptr3);
 
		strtok_r(request.url, ":", &save_ptr4);
		request.url = strtok_r(NULL , ":", &save_ptr4);
		request.portcheck = strtok_r(NULL,":", &save_ptr4);


		//Check specified port numb if null use 80
		if(request.portcheck == NULL){
			request.portcheck =  HTTPPORT;
		}

		//check for banned hosts

		if((check_blk_list(request.host)) < 0){

			error400(connfd);
			free(buf);
			return;

		}


		//check to see if page is in cache if not continue
		if((page_cache(request.url , connfd, args->ttl)) >= 0){

			free(buf);
			return;

		}

		//create new socket
		new_fd = socket(AF_INET, SOCK_STREAM, 0);

		//error handling	
		if(new_fd < 0){
			printf("Create Socket in handle_request failed\n");
			free(buf);
			return;
		}


		//mutex to lock cache resource
		pthread_mutex_lock(&ip_lock);
		
		//check in ip cache
		k = cache_IP(ip_cache, ip_c_size, request.host);

		//if error unlock resource and send 400
		if(k < 0){

			pthread_mutex_unlock(&ip_lock);
			error400(connfd);
			free(buf);
			return;
		}

		//get ip_ from cache	
		printf("HOST %s : IP %s\n\n", ip_cache[k].hostname, ip_cache[k].ip );
				
		//create sockaddr_in struct
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = inet_addr(ip_cache[k].ip);
		server.sin_port = htons(atoi(request.portcheck));

		//unlock resource
		pthread_mutex_unlock(&ip_lock);

		if(connect(new_fd, (struct sockaddr*)&server, sizeof(server)) <  0)
			printf("Failed to Connect to server:\n");
		
		//send http_header to server if fails send 400 to client
		if((send_to_server(new_fd, &request)) > 0){			


			//create a cache page to write to if failed print to console
			cache_file = create_cache_page(request.url);
			if(cache_file == NULL){
				printf("Caching Page Failed: %s\n", request.url);
			}
			
			while( i !=  0){
				//wait for socket to finish writing
				bzero(buf, MAXBUF);
				i = read( new_fd, buf, MAXBUF);
				write(connfd, buf, i);		//write to client
				fwrite( buf, 1, i, cache_file);	//write to cache page
				printf("Received %d byte Response\n", i);
			}
		
			fclose(cache_file);//close cache file when done

		}	
		//close connection to server	
		printf("closing connection\n");
		close(new_fd);
			
	}else{
		
		error400(connfd);
		

	}
	//free malloced(buffer);
	free(buf);
	return;

}


int serverSocket(int port){


	int socketfd, optval=1;
	struct sockaddr_in serveraddr;
	
	if((socketfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 )
		return -1;
	
	
	if( setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR,
		(const void*)&optval, sizeof(int)) < 0)
		return -1;

	bzero((char*) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)port);

	if (bind(socketfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr))
		< 0 )
		return -1;

	if(listen(socketfd, LISTENQ) < 0)
		return -1;


	return socketfd; 
	
}

char*  dnslookup(char* hostname){

	char  *ip_buffer, *return_IP;
	struct hostent *info;	

	info = gethostbyname(hostname);
	
	if(info == NULL){
		printf("dnslookup failed : %s\n", hostname);
		return NULL;
	}
	
	ip_buffer = inet_ntoa(*((struct in_addr*)info->h_addr_list[0]));
	
	return_IP = malloc(strlen(ip_buffer));
	
	strncpy(return_IP, ip_buffer, strlen(ip_buffer));

	return return_IP;
	
}


