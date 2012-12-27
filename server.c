#include "cs537.h"
#include "request.h"

// 
// server.c: A very, very simple web server
//
// To run:
//  server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;

pthread_t **cid;

typedef struct {
	int fd;
	long size, arrival, dispatch;
} request;

typedef struct {
	int requests;
	int fillptr;
	request **buffer;
} epoch;

epoch **epochs;
request **buffer;
int fillptr, useptr, max, numfull, algorithm, threadid;

// CS537: Parse the new arguments too
void getargs(int *port, int *threads, int *buffers, int *alg, int argc, char *argv[])
{
    if (argc != 5) {
		if(argc != 6 || strcmp(argv[4], "SFF-BS")) {
			fprintf(stderr, "Usage: %s <port> <threads> <buffers> <schedalg> [N (for SFF-BS only)]\n", argv[0]);
			exit(1);
		}
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *buffers = atoi(argv[3]);
	if(strcasecmp(argv[4], "FIFO") == 0) {
		*alg = -2;
	}
	else if(strcasecmp(argv[4], "SFF") == 0) {
		*alg = -1;
	}
	else if(strcasecmp(argv[4], "SFF-BS") == 0 && atoi(argv[5]) > 0) {
		*alg = atoi(argv[5]);
	}
	else {
		fprintf(stderr, "Usage: %s <port> <threads> <buffers> <schedalg> [N (for SFF-BS only)]\n", argv[0]);
		exit(1);
	}
}

int requestcmp(const void *first, const void *second) {
	return ((*(request **)first)->size - (*(request **)second)->size);
}


void *consumer(void *arg) {
	thread worker;
	worker.id = -1;
	worker.count = 0;
	worker.statics = 0;
	worker.dynamics = 0;
	struct timeval dispatch;
	while(1) {
		pthread_mutex_lock(&lock);
		while(numfull == 0) {
			pthread_cond_wait(&fill, &lock);
		}

		gettimeofday(&dispatch, NULL);

		if(worker.id < 0) {
			worker.id = threadid;
			threadid++;
		}
		worker.count++;

		request *req;

		//FIFO
		if(algorithm == -2) {
			req = (request *)buffer[useptr];
			useptr = (useptr + 1) % max;
		}
		//SFF
		else if(algorithm == -1) {
			req = (request *)buffer[0];
			buffer[0] = buffer[fillptr - 1];
			if(fillptr > 1) {
				qsort(buffer, fillptr, sizeof(*buffer), requestcmp);
			}
			fillptr--;
		}
		//SFF-BD
		else {
			epoch *epo = (epoch *)epochs[0];
			req = (request *)epo->buffer[0];
			epo->buffer[0] = epo->buffer[epo->fillptr - 1];
			if(epo->fillptr > 1) {
				qsort(epo->buffer, epo->fillptr, sizeof(*epo->buffer), requestcmp);
			}
			epo->fillptr--;

			// Finished epoch
			if(epo->fillptr == 0 && epo->requests >= algorithm && fillptr > 0) {
				int i = 0;
				for(i = 0; i < fillptr; i++) {
					epochs[i] = epochs[i + 1];
				}
				fillptr--;
			}			
		}
		req->dispatch = ((dispatch.tv_sec) * 1000 + dispatch.tv_usec/1000.0) + 0.5;
	 	numfull--;

		pthread_cond_signal(&empty);
		pthread_mutex_unlock(&lock);

		requestHandle(req->fd, req->arrival, req->dispatch, &worker);
		Close(req->fd);

	}
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, threads, buffers, alg, clientlen;
    struct sockaddr_in clientaddr;
	struct timeval arrival;

    getargs(&port, &threads, &buffers, &alg, argc, argv);

	max = buffers;
	numfull = fillptr = useptr = 0;
	algorithm = alg;
	// SFF-BS
	if(alg > 0) {
		epochs = malloc((buffers / alg) * sizeof(*epochs));
	}
	// FIFO or SFF
	else {
		buffer = malloc(buffers * sizeof(*buffer));
	}


	cid = malloc(threads*sizeof(*cid));

	int i;
	for(i = 0; i< threads; i++) {
		cid[i] = malloc(sizeof(pthread_t));
		pthread_create(cid[i], NULL, consumer, NULL);
	}

    // 
    // CS537: Create some threads...
    //

    listenfd = Open_listenfd(port);
    while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);
		gettimeofday(&arrival, NULL);

		pthread_mutex_lock(&lock);
		while(numfull == max) {
			pthread_cond_wait(&empty, &lock);
		}

		request *req = malloc(sizeof(request)); 
		//FIFO
		if(alg == -2) {
			buffer[fillptr] = req;	
			fillptr = (fillptr+1) % max;	
		}
		//SFF
		else if(alg == -1) {
			req->size = requestFileSize(connfd);
			buffer[fillptr] = req;
			fillptr++;
			if(fillptr > 1) {
				qsort(buffer, fillptr, sizeof(*buffer), requestcmp);
			}
		}
		//SFF-BD
		else {
			epoch *epo;
			if(epochs[fillptr] == NULL) {
				epo = malloc(sizeof(epoch));
				epo->buffer = malloc(alg * sizeof(*epo->buffer));
				epochs[fillptr] = epo;
			}
			else {
				epo = epochs[fillptr];
			}
			req->size = requestFileSize(connfd);
			epo->buffer[epo->fillptr] = req;
			epo->fillptr++;
			epo->requests++;
			if(epo->fillptr > 1) {
				qsort(epo->buffer, epo->fillptr, sizeof(*epo->buffer), requestcmp);
			}

			if(epo->requests >= alg) {
				fillptr++;
			}
		}
		req->fd = connfd;
		req->arrival = ((arrival.tv_sec) * 1000 + arrival.tv_usec/1000.0) + 0.5;
		numfull++;

		pthread_cond_signal(&fill);
		pthread_mutex_unlock(&lock);

    }

}


    


 
