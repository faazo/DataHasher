# TCP Hashing

## Brief

This project consists of a client and server, each to be run separately. In this setup, a client is looking to obtain hashes for some of its file's contents. Each message that the client sends will include details about the data segment that is to be hashed. The server will hash (SHA-256) the segment and respond to the client. When this information is received, the client will print the hexidecimal representation of the hash to console.

## Usage

### Server

Arguments
```
usage: ./server [-p serverPort] [-s salt]

required parameters:
  -p                The port to bind and listen to

optional parameters:
  -s                A salt to use in computing all hashes

```
Example call
```
./server -p 4500 -s aRandomSalt
```

### Client

Arguments
```
usage: ./client [-a targetServerIp] [-p targetServerPort] [-n] [--smin=minSize] [--smax=maxSize] [-f]

required parameters:
  -a                The target server's IP address 
  -p                The target server's listening port
  -n                The number of hashRequests the client will send to the server. This value must be a base 10 integer and >= 0
  --smin            Minimum size of a data pyaload for each hashRequest. This value must be a base 10 integer and >= 1
  --smax            Maximum size of a data payload for each hashRequest. This value must be a base 10 integer and <= 2^24
  -f                Filename to read - must have enough data to support n requests at the maximum length (smax)

```
Example call
```
./client -a 127.0.0.1 -p 4500 -n 100 --smin=128 --smax=512 -f /dev/zero
```

## Technical breakdown

### 1. Initialization

The client will send the server a message with
* Type: 4-byte integer set to value 1
* N: 4-byte integer value corresponding to the number of hashRequests (N) that the client will be sending 

### 2. Acknowledgment

The server will send the client a message with
* Type: 4-byte integer set to value 2
* Length: 4-byte integer noting the length of all hashResponses. This will be N * 40

### 3. Hash Request

For each hash request (N), the client will send the server a message with
* Type: 4-byte integer set to value 3
* Length: 4-byte integer noting the length of the data payload (in number of bytes)
* Data: The payload containing the data segment to be hashed

### 4. Hash Response

For each hash response, the server will respond to the client with a message with
* Type: 4-byte integer set to value 4
* i: 4-byte integer noting the zero-based index of the response. Ex -> The first response will have i = 0; the last will be i = N - 1
* Hash i: 32-byte (256-bit) value noting the hash of the data contained in the i HashRequest

## Credits

The contents of the "hash" folder, starter argument parsing code, and details for technical breakdown were provided by instructional staff. The "client.c", "server.c", and "Makefile" files were then developed and written by myself.

