#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <argp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <inttypes.h>
#include "hash/src/hash.c"
#include <pthread.h>

/******************************************************************************/
// Server args
struct server_arguments {
	int port;
	char *salt;
	size_t salt_len;
	_Bool port_check, salt_check;
};

//Thread structure
struct threadArgs {
    int clntSock;
	struct checksum_ctx *ctx;
};

/******************************************************************************/
// Exit function for errors
void exiter(char *str) {
	printf("Failure in %s\n", str);
	exit(EXIT_FAILURE);
}

/******************************************************************************/
// Send all bytes over through the socket
int sendAll(int sock, const void *type, size_t len) {
	ssize_t counter = 0, total = 0;
	const char *currBuff = type;

	// Ensures all bytes are sent over and error out if something fails to make it through
	while (total < len) {
		counter = send(sock, currBuff + total, len - total, 0);

		if (counter <= 0)
			return -1;

		total += counter;
	}
	return 0;
}

/******************************************************************************/
// Parse command line arguments
error_t server_parser(int key, char *arg, struct argp_state *state) {
	struct server_arguments *args = state->input;
	error_t ret = 0;
	int i = 0;

	switch(key) {
		case 'p':
			/* Validate that port is correct and a number, etc!! */
			for (; i < strlen(arg); i++) {
				if (isdigit(arg[i]) == 0) {
					argp_error(state, "Invalid option for a server port, must be a number");
				}
			}

			// These are privileged ports. Can't use em!
			if (atoi(arg) <= 1024) {
				argp_error(state, "Invalid option for a server port, it can't be this small!");
			}

			args->port = atoi(arg);
			args->port_check = 1;
			break;

		// Assign the salt variables if one is passed in
		case 's':
			args->salt_len = strlen(arg);
			args->salt = malloc(args->salt_len+1);
			args->salt_check = 1;
			strcpy(args->salt, arg);
			break;

		default:
			ret = ARGP_ERR_UNKNOWN;
			break;
	}
	return ret;
}

/******************************************************************************/
// Read in the CLI args
struct server_arguments server_parseopt(int argc, char *argv[]) {
	struct server_arguments args;

	/* bzero ensures that "default" parameters are all zeroed out */
	bzero(&args, sizeof(args));

	struct argp_option options[] = {
		{ "port", 'p', "port", 0, "The port to be used for the server" ,0},
		{ "salt", 's', "salt", 0, "The salt to be used for the server. Zero by default", 0},
		{0}
	};
	struct argp argp_settings = { options, server_parser, 0, 0, 0, 0, 0 };
	if (argp_parse(&argp_settings, argc, argv, 0, NULL, &args) != 0)
		exiter("server parsing");

	/* Validates that all input that was needed, was received. */
	if (args.port_check == 0)
		exiter("server port check");

	return args;
}

/******************************************************************************/
// Execution per thread/client
void *threadMain(void *thready) {

	uint8_t hashResp[32];
	uint32_t type, numReq, temp, payLen, count = 0;
	int clntSock, track;
	uint8_t *payload;

	// Thread args and extraction
	pthread_detach(pthread_self());
	clntSock = ((struct threadArgs *) thready)->clntSock;
	struct checksum_ctx *ctx = ((struct threadArgs *) thready)->ctx;
	free(thready);

	//Acknowledgement
	/********************************************************/
	track = 0;
	while (track < sizeof(type))
		track += recv(clntSock, &type + track, sizeof(type) - track, 0);

	if (ntohl(type) == 1) {

		//Acking the num requests
		track = 0;
		while (track < sizeof(numReq))
			track += recv(clntSock, &numReq + track, sizeof(numReq) - track, 0);

		//Dealing with passed in info
		type = htonl(2);
		temp = ntohl(numReq);
		numReq = htonl(temp * 40);

		if (sendAll(clntSock, &type, sizeof(type)) < 0)
			exiter("sending type from ack");

		if (sendAll(clntSock, &numReq, sizeof(numReq)) < 0)
			exiter("sending numReq from ack");
	}

	//HashResponse
	/********************************************************/
	while (count < temp) {
		//Actively listening for any incoming messages until all hashes have been sent
		track = 0;
		while (track < sizeof(type))
			track += recv(clntSock, &type + track, sizeof(type) - track, 0);

		if (ntohl(type) == 3) {
			//RECEIVING *****************************************************************
			//Receiving the payload length
			track = 0;
			while (track < sizeof(payLen))
				track  += recv(clntSock, &payLen + track, sizeof(payLen) - track, 0);

			//Receiving the payload
			payload = malloc(ntohl(payLen));
			track = 0;
			while (track < ntohl(payLen))
				track += recv(clntSock, payload + track, ntohl(payLen) - track, 0);

			type = htonl(4);

			//SENDING *******************************************************************
			if (sendAll(clntSock, &type, sizeof(type)) < 0)
				exiter("sending type on hashResponse");

			//Sending count
			count = htonl(count);
			if (sendAll(clntSock, &count, sizeof(count)) < 0)
				exiter("sending count on hashResponse");
			count = ntohl(count);
			count++;

			// Updating the hash response after it is checksumed
			checksum_finish(ctx, payload, ntohl(payLen), hashResp);

			// Sending the checksumed hash response
			track = 0;
			while (track < sizeof(hashResp))
				track += send(clntSock, hashResp + track, sizeof(hashResp) - track, 0);

			// resetting the checksum and freeing the payload for reuse
			checksum_reset(ctx);
			free(payload);
		}
	}
	return NULL; // Makes the compiler happy
}

/******************************************************************************/
int main(int argc, char *argv[]) {

	uint8_t *saltMan;
	int i = 0, clntSock, servSock;
	unsigned int clntLen;

	struct sockaddr_in servAdd;		 	// Local address
	struct sockaddr_in clientAddr;		// Client address
	struct server_arguments ele;		// Structure to hold all the cmd line args
	struct checksum_ctx *ctx;			// Checksum structure

	// Thread variables
	struct threadArgs *thready;
	pthread_t threadyID;

	ele = server_parseopt(argc, argv); //parse options as the server would

/******************************************************************************/

	/* Create socket for incoming connections */
	if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		exiter("creating server in socket");

	/* Construct local address structure */
	servAdd.sin_family		 = AF_INET;                /* Internet address family */
	servAdd.sin_addr.s_addr  = htonl(INADDR_ANY); 	   /* Any incoming interface */
	servAdd.sin_port 		 = htons(ele.port);      	   /* Local port */

	/* Bind to the local address */
	if (bind(servSock, (struct sockaddr *) &servAdd, sizeof(servAdd)) < 0)
		exiter("failed binding in socket");

	/* Mark the socket so it will listen for incoming connections */
	if (listen(servSock, 5) < 0)
		exiter("listening to server");

	/* Set the size of the in-out parameter */
	clntLen = sizeof(clientAddr);

	// Creating the checksum with no salt
	if (ele.salt_check == 0 || ele.salt_len == 0) {
		saltMan = NULL;
		ctx = checksum_create(saltMan, 0);
	}

	// Salt was passed in, so create checksum using it
	else {
		saltMan = malloc(ele.salt_len);
		for (i = 0; i < ele.salt_len; i++) {
			saltMan[i] = ele.salt[i];
		}
		ctx = checksum_create(saltMan, sizeof(saltMan) -1);
	}

/******************************************************************************/
	for(;;) { /* Run forever */

		/* Wait for a client to connect */
		if ((clntSock = accept(servSock, (struct sockaddr *) &clientAddr, &clntLen)) < 0)
			exiter("waiting for client to accept");

		// Create a thread for this client
		thready = malloc(sizeof(struct threadArgs));
		thready->clntSock = clntSock;
		thready->ctx = ctx;

		if (pthread_create(&threadyID, NULL, threadMain, (void*) thready) < 0)
			printf("ouchies!");
	}

 /*****************************************************************************/
	// Will only get here if the program is cancelled/exited for manual cleanup of memory
	// Add code to handle cntrl c + exit behavior so that this will get done
	// as is, the memory will still be automatically cleaned up
	// checksum_destroy(ctx);
	// free(saltMan);
	// free(ele.salt);
}
