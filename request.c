//
// request.c: Does the bulk of the work for the web server.
// 

#include "cs537.h"
#include "request.h"

void requestError(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
   char buf[MAXLINE], body[MAXBUF];

   printf("Request ERROR\n");

   // Create the body of the error message
   sprintf(body, "<html><title>CS537 Error</title>");
   sprintf(body, "%s<body bgcolor=""fffff"">\r\n", body);
   sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
   sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
   sprintf(body, "%s<hr>CS537 Web Server\r\n", body);

   // Write out the header information for this response
   sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
   Rio_writen(fd, buf, strlen(buf));
   printf("%s", buf);

   sprintf(buf, "Content-Type: text/html\r\n");
   Rio_writen(fd, buf, strlen(buf));
   printf("%s", buf);

   sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
   Rio_writen(fd, buf, strlen(buf));
   printf("%s", buf);

   // Write out the content
   Rio_writen(fd, body, strlen(body));
   printf("%s", body);

}


//
// Reads and discards everything up to an empty text line
//
void requestReadhdrs(rio_t *rp)
{
   char buf[MAXLINE];

   Rio_readlineb(rp, buf, MAXLINE);
   while (strcmp(buf, "\r\n")) {
      Rio_readlineb(rp, buf, MAXLINE);
   }
   return;
}

//
// Return 1 if static, 0 if dynamic content
// Calculates filename (and cgiargs, for dynamic) from uri
//
int requestParseURI(char *uri, char *filename, char *cgiargs) 
{
   char *ptr;

   if (!strstr(uri, "cgi")) {
      // static
      strcpy(cgiargs, "");
      sprintf(filename, ".%s", uri);
      if (uri[strlen(uri)-1] == '/') {
         strcat(filename, "home.html");
      }
      return 1;
   } else {
      // dynamic
      ptr = index(uri, '?');
      if (ptr) {
         strcpy(cgiargs, ptr+1);
         *ptr = '\0';
      } else {
         strcpy(cgiargs, "");
      }
      sprintf(filename, ".%s", uri);
      return 0;
   }
}

//
// Fills in the filetype given the filename
//
void requestGetFiletype(char *filename, char *filetype)
{
   if (strstr(filename, ".html")) 
      strcpy(filetype, "text/html");
   else if (strstr(filename, ".gif")) 
      strcpy(filetype, "image/gif");
   else if (strstr(filename, ".jpg")) 
      strcpy(filetype, "image/jpeg");
   else 
      strcpy(filetype, "test/plain");
}

void requestServeDynamic(int fd, char *filename, char *cgiargs, long arrival, long dispatch, thread *worker)
{
   char buf[MAXLINE], *emptylist[] = {NULL};

   // The server does only a little bit of the header.  
   // The CGI script has to finish writing out the header.
   sprintf(buf, "HTTP/1.0 200 OK\r\n");
   sprintf(buf, "%sServer: CS537 Web Server\r\n", buf);

	 /* CS537: Your statistics go here -- fill in the 0's with something useful! */
	 sprintf(buf, "%s Stat-req-arrival: %ld\r\n", buf, arrival);
	 sprintf(buf, "%s Stat-req-dispatch: %d\r\n", buf, (int)(dispatch - arrival));
	 sprintf(buf, "%s Stat-thread-id: %d\r\n", buf, worker->id);
	 sprintf(buf, "%s Stat-thread-count: %d\r\n", buf, worker->count);
	 sprintf(buf, "%s Stat-thread-static: %d\r\n", buf, worker->statics);
	 sprintf(buf, "%s Stat-thread-dynamic: %d\r\n", buf, worker->dynamics);

   Rio_writen(fd, buf, strlen(buf));

   if (Fork() == 0) {
      /* Child process */
      Setenv("QUERY_STRING", cgiargs, 1);
      /* When the CGI process writes to stdout, it will instead go to the socket */
      Dup2(fd, STDOUT_FILENO);
      Execve(filename, emptylist, environ);
   }
   Wait(NULL);
}


void requestServeStatic(int fd, char *filename, int filesize, long arrival, long dispatch, thread *worker) 
{
   int srcfd;
   char *srcp, filetype[MAXLINE], buf[MAXBUF];
   struct timeval beforeread, afterread;
   int read, complete;

	gettimeofday(&beforeread, NULL);

   requestGetFiletype(filename, filetype);

   srcfd = Open(filename, O_RDONLY, 0);

   // Rather than call read() to read the file into memory, 
   // which would require that we allocate a buffer, we memory-map the file
   srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
   Close(srcfd);

	gettimeofday(&afterread, NULL);

	read = ((afterread.tv_sec - beforeread.tv_sec) * 1000 + (afterread.tv_usec - beforeread.tv_usec)/1000.0) + 0.5;
	complete = (((afterread.tv_sec) * 1000 + (afterread.tv_usec)/1000.0) + 0.5) - arrival;

   // put together response
   sprintf(buf, "HTTP/1.0 200 OK\r\n");
   sprintf(buf, "%sServer: CS537 Web Server\r\n", buf);

	 // CS537: Your statistics go here -- fill in the 0's with something useful!
	 sprintf(buf, "%s Stat-req-arrival: %ld\r\n", buf, arrival);
	 sprintf(buf, "%s Stat-req-dispatch: %d\r\n", buf, (int)(dispatch - arrival));
	 sprintf(buf, "%s Stat-req-read: %d\r\n", buf, read);
	 sprintf(buf, "%s Stat-req-complete: %d\r\n", buf, complete); 
	 sprintf(buf, "%s Stat-thread-id: %d\r\n", buf, worker->id);
	 sprintf(buf, "%s Stat-thread-count: %d\r\n", buf, worker->count);
	 sprintf(buf, "%s Stat-thread-static: %d\r\n", buf, worker->statics);
	 sprintf(buf, "%s Stat-thread-dynamic: %d\r\n", buf, worker->dynamics);

   sprintf(buf, "%sContent-Length: %d\r\n", buf, filesize);
   sprintf(buf, "%sContent-Type: %s\r\n\r\n", buf, filetype);

   Rio_writen(fd, buf, strlen(buf));

   //  Writes out to the client socket the memory-mapped file 
   Rio_writen(fd, srcp, filesize);
   Munmap(srcp, filesize);

}

// handle a request
void requestHandle(int fd, long arrival, long dispatch, thread *worker)
{

   int is_static;
   struct stat sbuf;
   char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
   char filename[MAXLINE], cgiargs[MAXLINE];
   rio_t rio;

   Rio_readinitb(&rio, fd);
   Rio_readlineb(&rio, buf, MAXLINE);
   sscanf(buf, "%s %s %s", method, uri, version);

   printf("%s %s %s\n", method, uri, version);

   if (strcasecmp(method, "GET")) {
      requestError(fd, method, "501", "Not Implemented", "CS537 Server does not implement this method");
      return;
   }
   requestReadhdrs(&rio);

   is_static = requestParseURI(uri, filename, cgiargs);
   if (stat(filename, &sbuf) < 0) {
      requestError(fd, filename, "404", "Not found", "CS537 Server could not find this file");
      return;
   }

   if (is_static) {
      if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
         requestError(fd, filename, "403", "Forbidden", "CS537 Server could not read this file");
         return;
      }
	  worker->statics++;
      requestServeStatic(fd, filename, sbuf.st_size, arrival, dispatch, worker);
   } else {
      if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
         requestError(fd, filename, "403", "Forbidden", "CS537 Server could not run this CGI program");
         return;
      }
 	  worker->dynamics++;
      requestServeDynamic(fd, filename, cgiargs, arrival, dispatch, worker);
   }
}

long requestFileSize(int fd) {
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];	
	struct stat s;

	recv(fd, buf, sizeof(buf), MSG_PEEK);
	
   	sscanf(buf, "%s %s %s\n", method, uri, version);

	requestParseURI(uri, filename, cgiargs);

	Stat(filename, &s);

	return (long)s.st_size;
}


