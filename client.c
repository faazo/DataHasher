#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <argp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <inttypes.h>

/**********************************************************************************************************************************/

struct client_arguments {
	struct sockaddr_in info; //Stores IP address + port
	int hashnum;
	int smin;
	int smax;
	char *filename;
	_Bool ip_check, port_check, hash_check, min_check, max_check, file_check;
};

/**********************************************************************************************************************************/

// Error exit function
void exiter(char *str) {
	printf("Failure in %s\n", str);
	exit(EXIT_FAILURE);
}

/**********************************************************************************************************************************/

// Verifies all characters in the string are integers and errors out if not
int int_check(char *arg, struct argp_state *state) {
	int i = 0;
	/* validate arg */
	for (; i < strlen(arg); i++) {
		if (isdigit(arg[i]) == 0) {
			argp_error(state, "Invalid input");
		}
	}
	return 1;
}

/**********************************************************************************************************************************/

// Send all bytes over through the socket
int sendAll(int sock, const void *type, size_t len) {
	ssize_t counter = 0, total = 0;
	const char *currBuff = type;

	while (total < len) {
		counter = send(sock, currBuff + total, len - total, 0);

		if (counter <= 0)
			return -1;

		total += counter;
	}
	return 0;
}

/************************************************************************************************************************/

// Parse command line arguments
error_t client_parser(int key, char *arg, struct argp_state *state) {
	struct client_arguments *args = state->input;
	error_t ret = 0;
	int len;

	switch(key) {
		case 'a':
		args->info.sin_family = AF_INET;
		args->info.sin_addr.s_addr = inet_addr(arg);
		args->ip_check = 1;
		break;

		case 'p':
		int_check(arg, state);
		args->info.sin_port = htons(atoi(arg));
		args->port_check = 1;
		break;

		case 'n':
		if (!int_check(arg, state) || atoi(arg) < 0) {
			argp_error(state, "Invalid option for a hasrequest, it can't be negative!\n");
		}

		args->hashnum = atoi(arg);
		args->hash_check = 1;
		break;

		case 300:
		if (!int_check(arg, state) || atoi(arg) < 1) {
			argp_error(state, "Invalid option for a min payload, it can't be less than 1!\n");
		}

		args->smin = atoi(arg);
		args->min_check = 1;
		break;

		case 301:
		if (!int_check(arg, state) || atoi(arg) > 16777216) {
			argp_error(state, "Invalid option for a max payload\n");
		}

		args->smax = atoi(arg);
		args->max_check = 1;
		break;

		case 'f':
		/* validate file */
		len = strlen(arg);
		args->filename = malloc(len + 1);
		strcpy(args->filename, arg);
		args->file_check = 1;
		break;

		default:
		ret = ARGP_ERR_UNKNOWN;
		break;
	}
	return ret;
}

/**********************************************************************************************************************************/

struct client_arguments client_parseopt(int argc, char *argv[]) {
	struct argp_option options[] = {
		{ "addr", 'a', "addr", 0, "The IP address the server is listening at", 0},
		{ "port", 'p', "port", 0, "The port that is being used at the server", 0},
		{ "hashreq", 'n', "hashreq", 0, "The number of hash requests to send to the server", 0},
		{ "smin", 300, "minsize", 0, "The minimum size for the data payload in each hash request", 0},
		{ "smax", 301, "maxsize", 0, "The maximum size for the data payload in each hash request", 0},
		{ "file", 'f', "file", 0, "The file that the client reads data from for all hash requests", 0},
		{0}
	};

	struct argp argp_settings = { options, client_parser, 0, 0, 0, 0, 0 };

	struct client_arguments args;
	bzero(&args, sizeof(args));

	if (argp_parse(&argp_settings, argc, argv, 0, NULL, &args) != 0)
		exiter("parse");

	/* Validates that all input that was needed, was received. */
	if (args.ip_check == 0 || args.port_check == 0 || args.hash_check == 0 ||
		args.min_check == 0 ||  args.max_check == 0 || args.file_check == 0)
		exiter("input checking");

	return args;
	}

/**********************************************************************************************************************************/
int main(int argc, char *argv[]) {
	int sock, random, i, counter = 0, track;
	uint32_t type, numReq, payLen, count, updatedReq;
	uint8_t hashResp[32];
	char* payload;
	struct client_arguments ele;
	struct sockaddr_in servAdd;
	FILE* toOpen;

	ele = client_parseopt(argc, argv); //parse options as the client would

	if (ele.smin > ele.smax)
		exiter("min > max");

	//Creating and connecting socket
	/************************************************************************/

	/* Create a reliable, stream socket using TCP */
	if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		exiter("socket creation on client");

	/* Construct the server address structure */
	bzero(&servAdd, sizeof(servAdd));
	servAdd = ele.info;

	/* Establish the connection to the echo server */
	if (connect(sock, (struct sockaddr *)&servAdd, sizeof(servAdd)) < 0)
		exiter("connecion failed");

	toOpen = fopen(ele.filename, "r");
	if(toOpen == NULL)
		exiter("file opening");

	//Initialization
	/************************************************************************/

	type = htonl(1);
	numReq = htonl(ele.hashnum);

	if (sendAll(sock, &type, sizeof(type)) < 0)
		exiter("type Initialization");

	if (sendAll(sock, &numReq, sizeof(numReq)) < 0)
		exiter("numReq Initialization");

	/************************************************************************/
	while (counter < ele.hashnum) {

		//Receive the Acknowledgement
		track = 0;
		while (track < sizeof(type))
			track += recv(sock, &type + track, sizeof(type) - track, 0);

		//Hash Request
		if (ntohl(type) == 2) {
			track = 0;
			while (track < sizeof(updatedReq))
				track += recv(sock, &updatedReq + track, sizeof(updatedReq) - track, 0);

			for (i = 0; i < ele.hashnum; i++) {

				type = htonl(3);
				random = (rand() % (ele.smax - ele.smin + 1)) + ele.smin;
				payload = malloc(random);
				payLen = htonl(fread(payload, 1 , random, toOpen));

				if (ntohl(payLen) != random)
					exiter("didnt read the write # of bytes");

				//Send type
				if (sendAll(sock, &type, sizeof(type)) < 0)
					exiter("type HashRequest");

				//Send payload length
				if (sendAll(sock, &payLen, sizeof(payLen)) < 0)
					exiter("numReq Initialization");

				//Send the payload
				track = 0;
				while (track < random)
					track += send(sock, payload + track, random - track, 0);

				free(payload);
			}
		}

		//PRINTS OUT THE HASH RESPONSES AS THEY COME IN
		if (ntohl(type) == 4) {

			track = 0;
			while (track < sizeof(count))
				track += recv(sock, &count + track, sizeof(count) - track, 0);

			track = 0;
			while (track < sizeof(hashResp))
				track += recv(sock, &hashResp + track, sizeof(hashResp) - track, 0);

			//Printing the hashes
			printf("%d: 0x", ntohl(count) + 1);
			for(i = 0; i < 32; i++) {
				printf("%02x", hashResp[i]);
			}
			putchar('\n');
			counter++;
		}
	}

	/************************************************************************/
	free(ele.filename);
	close(sock);
	fclose(toOpen);
	return 0;
}
