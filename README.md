# RemoteOps - Remote System Monitoring and Management Tool

## Module
IE3090 - Network Programming

## Student Information

Registration Number: IT24102234
SID: 4322
Authentication Token: OPS-2234
Agent TCP Port: 9410
Agent Source: agent_234.c
Controller Source: controller_234.c
Makefile: Makefile_234
Log File: remoteops_IT24102234.log
Agent Storage: ./agentfiles/IT24102234/

## Project Overview

RemoteOps is a client-server remote system monitoring and management tool implemented in C using the standard BSD socket API.

The system consists of two components:

- Agent - runs on the managed Linux machine and accepts Controller connections.
- Controller - connects to the Agent and sends authenticated management and monitoring commands.

TCP is used for reliable control communication, while UDP is used for periodic system monitoring.

## Architecture

The RemoteOps Agent uses pthread-based concurrency. A separate pthread is created for each connected Controller, allowing multiple Controllers to communicate with the Agent simultaneously.

The Agent supports at least five simultaneous Controller connections.

## TCP Commands

The implemented TCP commands are:

AUTH
SYSINFO
LISTPROC
EXEC
PUT
GET
MONITOR START
MONITOR STOP
QUIT

## Authentication

The Controller must authenticate before using other commands.

Authentication token:

OPS-2234

Successful authentication returns:

OK AUTHENTICATED SID:4322

## SYSINFO

The SYSINFO command returns CPU load, memory usage and system uptime.

Example:

SYSINFO

## LISTPROC

LISTPROC returns a snapshot of running processes.

Example:

LISTPROC

## EXEC

Only the following commands are permitted:

DATE
UPTIME
DISKFREE
HOSTNAME
WHOAMI

Non-whitelisted commands are rejected.

Example:

EXEC LS

returns:

ERR 002 COMMAND_NOT_ALLOWED SID:4322

## File Transfer

Files can be uploaded using:

PUT <filename> <filesize>

Files are stored in:

./agentfiles/IT24102234/

Files can be downloaded using:

GET <filename>

PUT and GET use exact byte counts for raw file data.

## UDP Monitoring

Monitoring is started using:

MONITOR START <udp_port>

and stopped using:

MONITOR STOP

The Agent periodically sends SYSINFO-style UDP datagrams to the Controller.

The monitoring interval is 2 seconds.

Each monitoring message contains:

SID:4322

## TCP Message Framing

The implementation uses buffered TCP message framing so that partial messages and multiple commands received in a single TCP transmission can be processed correctly.

File transfers use exact byte counts for raw file data.

## Concurrency

The Agent uses POSIX pthreads.

Each Controller connection is handled by a separate thread.

The implementation was tested with five simultaneous Controller connections.

## Logging

The Agent maintains the following event log:

remoteops_IT24102234.log

The log records:

- Agent startup
- Controller connections
- Authentication
- Commands
- File transfers
- UDP monitoring
- Graceful disconnections
- Ungraceful disconnections
- Connection closure

## Build

Compile the project using:

make -f Makefile_234

Clean the compiled programs using:

make -f Makefile_234 clean

## Run Agent

Start the Agent using:

./agent_234

The Agent listens on TCP port 9410.

## Run Controller

In another terminal run:

./controller_234

The Controller connects to:

127.0.0.1:9410

The default UDP monitoring port is 9500.

## Testing

The implementation was tested for:

- Authentication
- SYSINFO
- LISTPROC
- Whitelisted EXEC commands
- Rejection of non-whitelisted EXEC commands
- PUT and GET file transfer
- File integrity
- UDP monitoring
- Multiple simultaneous Controllers
- Five simultaneous Controller connections
- TCP message framing
- Graceful disconnect
- Ungraceful disconnect
- Event logging
- Personalized SID responses

## Personalization Summary

Registration Number: IT24102234
Agent Port: 9410
SID: 4322
Authentication Token: OPS-2234
Agent Source: agent_234.c
Controller Source: controller_234.c
Makefile: Makefile_234
Log File: remoteops_IT24102234.log
Storage Directory: ./agentfiles/IT24102234/
ZIP Name: IE3090_IT24102234.zip

## Technologies

C
Linux
GCC
BSD Sockets API
TCP
UDP
POSIX Threads
Make
Git
GitHub
