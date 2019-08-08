#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ev.h>

#define BUFLEN 8192
static uint32_t g_num_clients;

static char   *rdata = NULL; 
static ssize_t  rlen = 0; // read data length
static char   *wdata = NULL;
static ssize_t  wlen = 0;
static ssize_t  slen = 0;

// HTTP 
static void streaming_write_callback(struct ev_loop *loop, struct ev_io *watcher, int events) {
  
  
  if(EV_ERROR & events) {
	  perror("Invalid event");
	  return;
  }
  
  ssize_t send_bytes = send(watcher->fd, wdata + slen, wlen - slen, 0);
	
  if(send_bytes < 0) {
	  perror("Read error");
	  return;
  }
  
  if (send_bytes != 0){
	  slen += send_bytes;
	  if (slen < wlen) return; // not yet complete send
	
	  // complete send
	  printf("write data send end\n");
  }

  
  // free client_io
  free(wdata);
  wdata = NULL;
  wlen = 0;
  slen = 0;

  if(g_num_clients > 0) {
	  g_num_clients--;
	  printf("One client disconnected. Total now %u clients.\n", g_num_clients);
  }

  // stop_and_clean_up_socket_watcher(loop, watcher);
  ev_io_stop(loop, watcher);
  shutdown(watcher->fd, SHUT_RDWR);
  close(watcher->fd);
  free(watcher);

  return;
}




// HTTP recv callback
static void streaming_read_callback(struct ev_loop *loop, struct ev_io *watcher, int events) {
	char buffer[BUFLEN];
	ssize_t read_bytes;


	if(EV_ERROR & events) {
		perror("Invalid event");
		return;
	}

	read_bytes = recv(watcher->fd, buffer, BUFLEN, 0);

	if(read_bytes < 0) {
		perror("Read error");
		
		return;
	}

	// work for received data
	if(read_bytes != 0) {
	  // resize recv memory area
	  if ((rdata = realloc(rdata, rlen + read_bytes + 1)) != NULL){
		  memcpy(rdata + rlen, buffer, read_bytes);		
		  rdata[rlen + read_bytes] = '\0';
		
		  // if not received all request data from client, return event loop
		  if (strstr(rdata, "\n\n") == NULL &&
			  strstr(rdata, "\r\n\r\n") == NULL
			   ) {
		    rlen += read_bytes;
		    return;
		  }
		
		  // all data received
		
		  printf("[%s]\n",rdata);
		
		
		  if (rdata != NULL) {

		    	// normal write
		    	//write(watcher->fd, rdata, rlen);					
		  
		    	// ansync write 
			  struct ev_io *write_watcher = (struct ev_io*) malloc (sizeof(struct ev_io));			
			  if (write_watcher != NULL){
			  
			    // Stop read watch for this socket
			    ev_io_stop(loop, watcher);
			    free(watcher);
			  
			    // copy rdata -> wdata
			    wlen  = rlen;
			    wdata = realloc(wdata, wlen);
			    memcpy(wdata, rdata, rlen);
			  
			    // Start write watcher for this socket
			    slen = 0; // send size reset				
			    ev_io_init(write_watcher, streaming_write_callback, watcher->fd, EV_WRITE);
			    ev_io_start(loop, write_watcher);
			    printf("write callback start\n");
			    return;
			}
		}
	  }
	}

	// free client_io
	free(rdata);
	rlen = 0;
	rdata = NULL;
	
	// Socket shutdown	  

	if(g_num_clients > 0) g_num_clients--;
	printf("close : %d\n", g_num_clients);

	
	// cleanup socket
	ev_io_stop(loop, watcher);
	shutdown(watcher->fd, SHUT_RDWR);
	close(watcher->fd);
	free(watcher);
	  
	return;
}
// HTTP accept callback
static void http_accept_callback(struct ev_loop *loop, struct ev_io *watcher, int events) {
	struct sockaddr_in6 client_addr6;
	socklen_t client_len;
	struct ev_io *client_watcher;
	int client_fd;


	if(EV_ERROR & events) {
		perror("Invalid event");
		return;
	}

	client_watcher = (struct ev_io*) malloc (sizeof(struct ev_io));
	if(client_watcher == NULL) {
		perror("Could not allocate memory for client watcher.");
		return;
	}

	client_len = sizeof(client_addr6);

	client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr6, &client_len);

	if (client_fd < 0) {
		perror("Accept error");
		return;
	}

	g_num_clients++;
	printf("One client connected. Total now %u clients.\n", g_num_clients);
	
	// start read watcher 
	ev_io_init(client_watcher, streaming_read_callback, client_fd, EV_READ);
	ev_io_start(loop, client_watcher);
}
// create http event
int httpd_event_init (struct ev_loop *main_loop, uint16_t port){
  
  static int fd6;
  static struct sockaddr_in6 addr6;
  static struct ev_io http_accept_watcher; // I/O watcher object
  
  
  //bzero(&addr6, sizeof(addr6));
  memset(&addr6, 0, sizeof(addr6));
  addr6.sin6_family = AF_INET6;
  addr6.sin6_port = htons(port);
  addr6.sin6_addr = in6addr_any;
  
  
  if( (fd6 = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0 ) {
	perror("Socket error");
	return -1;
  }
  
  if (bind(fd6, (const struct sockaddr *) &addr6, sizeof(addr6)) != 0) {
	perror("Bind error");
	return -1;
  }
  
  if (listen(fd6, 10) < 0) {
	perror("Listen error");
	return -1;
  }
  
  // init io event
  ev_io_init(&http_accept_watcher, http_accept_callback, fd6, EV_READ); 
  ev_io_start(main_loop, &http_accept_watcher); 
  return 0;
}
	



int main(int argc, const char* argv[]) {
  int port = 8881;
  if (argc > 1){
	printf("port : %s\n",argv[1]);
	port = atoi(argv[1]);
  }
  
  // event loop create
  struct ev_loop *main_loop = ev_default_loop(0);
  
  // streaming server setting
  if (httpd_event_init(main_loop, port) == -1){
	return -1; // error
  }
    
  // start loop
  ev_run(main_loop, 0);

  // destroy loop
  ev_loop_destroy(main_loop);

  return 0;
}
