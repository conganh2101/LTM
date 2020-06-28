// TCPClient.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS 
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <conio.h>
#include <string.h>
#include <stdlib.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <process.h>
#include <direct.h>
#include "md5.h"

#pragma comment (lib,"Ws2_32.lib")
#pragma warning(disable : 4996)

#define BUFF_SIZE                  2048
#define DATA_BUFSIZE               8192
#define MAX_SOCK                   10
#define RECEIVE                    0
#define SEND                       1
#define DIGEST_SIZE		           33



typedef struct _SOCKET_INFORMATION {
	WSAOVERLAPPED overlapped;
	SOCKET sockfd;
	CHAR buff[DATA_BUFSIZE];
	WSABUF dataBuff;
	DWORD sentBytes;
	DWORD recvBytes;
	DWORD operation;
} SOCKET_INFORMATION, *LPSOCKET_INFORMATION;

typedef struct _FILE_INFORMATION {
	char fileName[100];
	char		digest[DIGEST_SIZE];
	FILE*		file = NULL;
	int fileLen;
	int idx;
	int nLeft;
	char *fileBuffer;
}FILE_INFORMATION, *LPFILE_INFORMATION;

typedef struct message {
	char opcode;
	unsigned int length;
	long offset;
	char payload[BUFF_SIZE];
} message;

typedef enum messType {
	sendingData,
	uploadFile,
	downloadFile,
	fileNameDownload,
	fileNameUpload,
	success,
	fileExist,
	noFileExist,
	md5Code,
	fileCorrupted
} Type;


int userChoice();
void Menu();
void handleUserChoice();

void CALLBACK workerDownloadRoutine(DWORD error, DWORD transferredBytes, LPWSAOVERLAPPED overlapped, DWORD inFlags);
unsigned __stdcall workerDownloadThread(LPVOID lpParameter);
void CALLBACK workerUploadRoutine(DWORD error, DWORD transferredBytes, LPWSAOVERLAPPED overlapped, DWORD inFlags);
unsigned __stdcall workerUploadThread(LPVOID lpParameter);

void uploadFileToServer(char *filePath);
void downloadFileFromServer(char *filePath);


LPSOCKET_INFORMATION downloadSockets[MAX_SOCK];
LPFILE_INFORMATION downloadFiles[MAX_SOCK];
int nDownloadSockets = 0;
CRITICAL_SECTION downloadCriticalSection;

LPSOCKET_INFORMATION uploadSockets[MAX_SOCK];
LPFILE_INFORMATION uploadFiles[MAX_SOCK];
int nUploadSockets = 0;
CRITICAL_SECTION uploadCriticalSection;



SOCKET client;
sockaddr_in serverAddr;


WSAEVENT connUploadEvent;
WSAEVENT connDownloadEvent;

char name[100];


int main(int argc, char** argv)
{

	InitializeCriticalSection(&downloadCriticalSection);
	InitializeCriticalSection(&uploadCriticalSection);
	//Step 1: Inittiate WinSock
	WSADATA wsaData;
	WORD wVersion = MAKEWORD(2, 2);
	if (WSAStartup(wVersion, &wsaData))
		printf("Version is not supported\n");
	//Step 3: Specify server address
	char*  IP;
	unsigned long ulAddr;
	IP = (char *)argv[1];
	ulAddr = inet_addr(IP);

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons((unsigned short)atoi((char *)argv[2]));
	serverAddr.sin_addr.s_addr = ulAddr;

	if ((connUploadEvent = WSACreateEvent()) == WSA_INVALID_EVENT)
	{
		printf("WSACreateEvent() failed with error %d\n", WSAGetLastError());
		return 1;
	}

	if ((connDownloadEvent = WSACreateEvent()) == WSA_INVALID_EVENT)
	{
		printf("WSACreateEvent() failed with error %d\n", WSAGetLastError());
		return 1;
	}
	// Create a worker thread to service completed I/O requests	
	_beginthreadex(0, 0, workerDownloadThread, (LPVOID)connDownloadEvent, 0, 0);
	_beginthreadex(0, 0, workerUploadThread, (LPVOID)connUploadEvent, 0, 0);

	Menu();
	while (TRUE) {
		handleUserChoice();
	}

	return 0;
}

void downloadFileFromServer(char *filepath) {
	strcpy_s(name, filepath);
	if ((client = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET) {
		printf("Failed to get a socket %d\n", WSAGetLastError());
		exit(1);
	}
	//(optional) Set time-out for receiving
	int tv = 10000; //Time-out interval: 10000ms
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)(&tv), sizeof(int));
	//Step 4: Request to connect server
	if (connect(client, (sockaddr *)&serverAddr, sizeof(serverAddr))) {
		printf("Error! Cannot connect server. %d", WSAGetLastError());
		exit(1);
	}
	if (WSASetEvent(connDownloadEvent) == FALSE) {
		printf("WSASetEvent() failed with error %d\n", WSAGetLastError());
		exit(1);
	}
	if ((downloadFiles[nDownloadSockets] = (LPFILE_INFORMATION)GlobalAlloc(GPTR, sizeof(FILE_INFORMATION))) == NULL) {
		printf("GlobalAlloc() failed with error %d\n", GetLastError());
		exit(1);
	}
}

void uploadFileToServer(char *filepath) {
	strcpy_s(name, filepath);

	FILE *file;
	file = fopen(name, "rb");
	if (!file)
	{
		fprintf(stderr, "Unable to open file %s", name);
		return;
	}
	fclose(file);

	if ((client = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET) {
		printf("Failed to get a socket %d\n", WSAGetLastError());
		exit(1);
	}
	//(optional) Set time-out for receiving
	int tv = 10000; //Time-out interval: 10000ms
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)(&tv), sizeof(int));
	//Step 4: Request to connect server
	if (connect(client, (sockaddr *)&serverAddr, sizeof(serverAddr))) {
		printf("Error! Cannot connect server. %d", WSAGetLastError());
		exit(1);
	}
	if (WSASetEvent(connUploadEvent) == FALSE) {
		printf("WSASetEvent() failed with error %d\n", WSAGetLastError());
		exit(1);
	}
	if ((downloadFiles[nDownloadSockets] = (LPFILE_INFORMATION)GlobalAlloc(GPTR, sizeof(FILE_INFORMATION))) == NULL) {
		printf("GlobalAlloc() failed with error %d\n", GetLastError());
		exit(1);
	}
}

void Menu() {
	printf("|============================|\n");
	printf("|        File Trasnfer       |\n");
	printf("|___________[Menu]___________|\n");
	printf("|                            |\n");
	printf("|      1.Upload              |\n");
	printf("|      2.Download            |\n");
	printf("|      3.Exit                |\n");
	printf("|                            |\n");
	printf("|============================|\n");
}

int userChoice() {
	char option[100];
	printf("Please choose your option:");
	gets_s(option);
	int n = atoi(option);
	if (n >= 1 || n <= 3)
		return n;
	else return userChoice();
}

void handleUserChoice() {
	int choice = userChoice();
	char filepath[100];

	switch (choice) {
	case 1:
		printf("\nEnter file name below:\n");
		gets_s(filepath);
		uploadFileToServer(filepath);
		break;
	case 2:
		printf("\nEnter file name below:\n");
		gets_s(filepath);
		downloadFileFromServer(filepath);
		break;
	case 3:
		printf("Exit");
		exit(1);
		break;
	}
}


unsigned __stdcall workerUploadThread(LPVOID lpParameter)
{
	DWORD flags;
	WSAEVENT events[1];
	DWORD index;
	DWORD sendBytes;

	events[0] = (WSAEVENT)lpParameter;
	while (TRUE) {
		while (TRUE) {
			index = WSAWaitForMultipleEvents(1, events, FALSE, WSA_INFINITE, TRUE);
			if (index == WSA_WAIT_FAILED) {
				printf("WSAWaitForMultipleEvents() failed with error %d\n", WSAGetLastError());
				return 1;
			}
			if (index != WAIT_IO_COMPLETION) {
				// An accept() call event is ready - break the wait loop
				break;
			}
		}

		WSAResetEvent(events[index - WSA_WAIT_EVENT_0]);

		EnterCriticalSection(&uploadCriticalSection);

		if (nUploadSockets == MAX_SOCK) {
			printf("Too many Socket.\n");
			closesocket(client);
			continue;
		}
		if ((uploadSockets[nUploadSockets] = (LPSOCKET_INFORMATION)GlobalAlloc(GPTR, sizeof(SOCKET_INFORMATION))) == NULL) {
			printf("GlobalAlloc() failed with error %d\n", GetLastError());
			return 1;
		}
		if ((uploadFiles[nUploadSockets] = (LPFILE_INFORMATION)GlobalAlloc(GPTR, sizeof(FILE_INFORMATION))) == NULL) {
			printf("GlobalAlloc() failed with error %d\n", GetLastError());
			return 1;
		}


		strcpy_s(uploadFiles[nUploadSockets]->fileName, name);
		FILE *file;
		//Open file
		file = fopen(uploadFiles[nUploadSockets]->fileName, "rb");
		if (!file)
		{
			fprintf(stderr, "Unable to open file %s", uploadFiles[nUploadSockets]->fileName);
			break;
		}

		//Get file length
		fseek(file, 0, SEEK_END);
		uploadFiles[nUploadSockets]->fileLen = ftell(file);
		fseek(file, 0, SEEK_SET);
		uploadFiles[nUploadSockets]->nLeft = uploadFiles[nUploadSockets]->fileLen;
		uploadFiles[nUploadSockets]->idx = 0;
		message sendMessage;
		sendMessage.opcode = fileNameUpload;

		uploadFiles[nUploadSockets]->fileBuffer = (char*)malloc(uploadFiles[nUploadSockets]->fileLen + 1);
		if (!uploadFiles[nUploadSockets]->fileBuffer)
		{
			fprintf(stderr, "Memory error!");
			fclose(file);
			break;
		}

		fread(uploadFiles[nUploadSockets]->fileBuffer, uploadFiles[nUploadSockets]->fileLen, 1, file);
		strcpy_s(sendMessage.payload, uploadFiles[nUploadSockets]->fileName);
		sendMessage.length = strlen(uploadFiles[nUploadSockets]->fileName);
		memcpy(uploadSockets[nUploadSockets]->buff, &sendMessage, sizeof(message));

		fclose(file);

		// gui message moi vs payload la ten file

		uploadSockets[nUploadSockets]->sockfd = client;

		ZeroMemory(&uploadSockets[nUploadSockets]->overlapped, sizeof(WSAOVERLAPPED));
		uploadSockets[nUploadSockets]->sentBytes = 0;
		uploadSockets[nUploadSockets]->recvBytes = 0;
		uploadSockets[nUploadSockets]->dataBuff.len = sizeof(message);
		uploadSockets[nUploadSockets]->dataBuff.buf = uploadSockets[nUploadSockets]->buff;
		uploadSockets[nUploadSockets]->operation = SEND;
		flags = 0;

		//First: send message contain file name to server

		if (WSASend(uploadSockets[nUploadSockets]->sockfd, &(uploadSockets[nUploadSockets]->dataBuff), 1,
			&sendBytes, 0, &(uploadSockets[nUploadSockets]->overlapped), workerUploadRoutine) == SOCKET_ERROR) {
			if (WSAGetLastError() != WSA_IO_PENDING) {
				printf("WSASend() failed with error %d\n", WSAGetLastError());
				return 1;
			}
		}
		printf("\nSocket %d got connected...\n", client);
		nUploadSockets++;
		LeaveCriticalSection(&uploadCriticalSection);
	}

	return 0;
}

void CALLBACK workerUploadRoutine(DWORD error, DWORD transferredBytes, LPWSAOVERLAPPED overlapped, DWORD inFlags)
{
	DWORD sendBytes, recvBytes;
	DWORD Flags;

	LPSOCKET_INFORMATION sockInfo = (LPSOCKET_INFORMATION)overlapped;

	if (error != 0)
		printf("I/O operation failed with error %d\n", error);

	if (transferredBytes == 0)
		printf("Closing socket %d\n\n", sockInfo->sockfd);

	if (error != 0 || transferredBytes == 0) {
		//Find and remove socket
		EnterCriticalSection(&uploadCriticalSection);

		int index;
		for (index = 0; index < nUploadSockets; index++)
			if (uploadSockets[index]->sockfd == sockInfo->sockfd)
				break;

		closesocket(uploadSockets[index]->sockfd);
		GlobalFree(uploadSockets[index]);
		GlobalFree(uploadFiles[index]);

		for (int i = index; i < nUploadSockets - 1; i++)
		{
			uploadSockets[i] = uploadSockets[i + 1];
			uploadFiles[i] = uploadFiles[i + 1];
		}
		nUploadSockets--;

		LeaveCriticalSection(&uploadCriticalSection);

		return;
	}

	int index;
	for (index = 0; index < nUploadSockets; index++)
		if (uploadSockets[index]->sockfd == sockInfo->sockfd)
			break;

	if (sockInfo->operation == SEND) {
		sockInfo->sentBytes += transferredBytes;
		sockInfo->recvBytes = 0;
	}
	else if (sockInfo->operation == RECEIVE) {
		sockInfo->recvBytes += transferredBytes;
		sockInfo->sentBytes = 0;
	}

	if (sockInfo->recvBytes > sockInfo->sentBytes)
	{// after receive from server
		if (sockInfo->recvBytes < sizeof(message))
		{// if receive bytes is less than message size
		 // post another WSARecv
			ZeroMemory(&(sockInfo->overlapped), sizeof(OVERLAPPED));
			sockInfo->dataBuff.buf = sockInfo->buff + sockInfo->sentBytes;
			sockInfo->dataBuff.len = sizeof(message) - sockInfo->recvBytes;
			sockInfo->operation = RECEIVE;

			if (WSARecv(sockInfo->sockfd,
				&(sockInfo->dataBuff),
				1,
				&transferredBytes,
				0,
				&(sockInfo->overlapped),
				workerUploadRoutine) == SOCKET_ERROR) {
				if (WSAGetLastError() != ERROR_IO_PENDING) {
					printf("WSARecv1() failed with error %d\n", WSAGetLastError());
					return;
				}
			}
		}
		else if (sockInfo->recvBytes == sizeof(message))
		{// if receive message to annouce result from server
		 // process the information
			message  *recvMessage;
			recvMessage = (message *)sockInfo->dataBuff.buf;
			if (recvMessage->opcode == uploadFile)
			{   // receive message that server allow to begin upload file
				// because file is not existing on server
				message sendMessage;
				sendMessage.opcode = md5Code;
				MD5 md5;
				char *digest = md5.digestFile(uploadFiles[index]->fileName);
				strcpy_s(sendMessage.payload, digest);
				sendMessage.length = strlen(digest);

				memcpy(sockInfo->buff, &sendMessage, sizeof(message));

				//Third: begin to sending data of file to server
				ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
				sockInfo->sentBytes = 0;
				sockInfo->dataBuff.buf = sockInfo->buff;
				sockInfo->dataBuff.len = sizeof(message);
				sockInfo->operation = SEND;
				if (WSASend(sockInfo->sockfd,
					&(sockInfo->dataBuff),
					1,
					&sendBytes,
					0,
					&(sockInfo->overlapped),
					workerUploadRoutine) == SOCKET_ERROR) {
					if (WSAGetLastError() != WSA_IO_PENDING) {
						printf("WSASend() failed with error %d\n", WSAGetLastError());
						return;
					}
				}
            }
			else if (recvMessage->opcode == success)
			{   // message from server to annouce that
				// file has been upload successfully
				EnterCriticalSection(&uploadCriticalSection);

				int index;
				for (index = 0; index < nUploadSockets; index++)
					if (uploadSockets[index]->sockfd == sockInfo->sockfd)
						break;
				printf("Closing socket %d\n", uploadSockets[index]->sockfd);
				closesocket(uploadSockets[index]->sockfd);
				GlobalFree(uploadSockets[index]);
				GlobalFree(uploadFiles[index]);

				for (int i = index; i < nUploadSockets - 1; i++)
				{
					uploadSockets[i] = uploadSockets[i + 1];
					uploadFiles[i] = uploadFiles[i + 1];
				}
				nUploadSockets--;

				LeaveCriticalSection(&uploadCriticalSection);
				printf("File store at address: %s  in server \n", recvMessage->payload);
			}
			else if (recvMessage->opcode == fileExist)
			{
				// message from server to annouce that
				// file is existing on server
				EnterCriticalSection(&uploadCriticalSection);

				int index;
				for (index = 0; index < nUploadSockets; index++)
					if (uploadSockets[index]->sockfd == sockInfo->sockfd)
						break;
				printf("Closing socket %d\n", uploadSockets[index]->sockfd);
				closesocket(uploadSockets[index]->sockfd);
				GlobalFree(uploadSockets[index]);
				GlobalFree(uploadFiles[index]);

				for (int i = index; i < nUploadSockets - 1; i++)
				{
					uploadSockets[i] = uploadSockets[i + 1];
					uploadFiles[i] = uploadFiles[i + 1];
				}
				nUploadSockets--;

				LeaveCriticalSection(&uploadCriticalSection);
				printf("File existed  at address: %s in server\n", recvMessage->payload);
			}
			else if(recvMessage->opcode == fileCorrupted)
			{
				printf("File corrupted on server. Ready to restart upload again to server.");

				EnterCriticalSection(&uploadCriticalSection);

				int index;
				for (index = 0; index < nUploadSockets; index++)
					if (uploadSockets[index]->sockfd == sockInfo->sockfd)
						break;
				printf("Closing socket %d\n", uploadSockets[index]->sockfd);
				closesocket(uploadSockets[index]->sockfd);
				GlobalFree(uploadSockets[index]);
				printf("Upload file %s again\n", uploadFiles[index]->fileName);


				uploadFileToServer(uploadFiles[index]->fileName);


				GlobalFree(uploadFiles[index]);

				for (int i = index; i < nUploadSockets - 1; i++)
				{
					uploadSockets[i] = uploadSockets[i + 1];
					uploadFiles[i] = uploadFiles[i + 1];
				}
				nUploadSockets--;

				LeaveCriticalSection(&uploadCriticalSection);

			}
		}
	}
	else
	{// after sending to server
		if (sockInfo->sentBytes < sizeof(message))
		{   // if sent bytes is less than message size
			// post another WSASend

			ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
			sockInfo->dataBuff.buf = sockInfo->buff + sockInfo->sentBytes;
			sockInfo->dataBuff.len = sizeof(message) - sockInfo->sentBytes;
			sockInfo->operation = SEND;
			if (WSASend(sockInfo->sockfd,
				&(sockInfo->dataBuff),
				1,
				&sendBytes,
				0,
				&(sockInfo->overlapped),
				workerUploadRoutine) == SOCKET_ERROR) {
				if (WSAGetLastError() != WSA_IO_PENDING) {
					printf("WSASend() failed with error %d\n", WSAGetLastError());
					return;
				}
			}
		}
		else if (sockInfo->sentBytes == sizeof(message))
		{// after sent bytes equal to message size
			message  *sendMessage;
			sendMessage = (message *)sockInfo->dataBuff.buf;
			if (sendMessage->length == 0)
			{   // if sent message has length=0
				// post WSARecv to receive result from server
				ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
				sockInfo->recvBytes = 0;
				sockInfo->sentBytes = 0;
				Flags = 0;
				sockInfo->dataBuff.len = sizeof(message);
				sockInfo->dataBuff.buf = sockInfo->buff;
				sockInfo->operation = RECEIVE;
				if (WSARecv(sockInfo->sockfd,
					&(sockInfo->dataBuff),
					1,
					&recvBytes,
					&Flags,
					&(sockInfo->overlapped),
					workerUploadRoutine) == SOCKET_ERROR) {
					if (WSAGetLastError() != WSA_IO_PENDING) {
						printf("WSARecv() failed with error %d\n", WSAGetLastError());
						return;
					}
				}
			}
			else if (sendMessage->length > 0)
			{
				if (sendMessage->opcode == fileNameUpload) {
					//Second: after sending file name to server
					// post WSARecv to confirm file is existing on server or not
					ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
					sockInfo->recvBytes = 0;
					sockInfo->sentBytes = 0;
					Flags = 0;
					sockInfo->dataBuff.len = sizeof(message);
					sockInfo->dataBuff.buf = sockInfo->buff;
					sockInfo->operation = RECEIVE;
					if (WSARecv(sockInfo->sockfd,
						&(sockInfo->dataBuff),
						1,
						&recvBytes,
						&Flags,
						&(sockInfo->overlapped),
						workerUploadRoutine) == SOCKET_ERROR) {
						if (WSAGetLastError() != WSA_IO_PENDING) {
							printf("WSARecv() failed with error %d\n", WSAGetLastError());
							return;
						}
					}
				}
				else if (sendMessage->opcode == sendingData)
				{
					if (uploadFiles[index]->nLeft > 0)
					{// if file still contains data that havent been sent
					 // post another WSASent to send remain data to server (length>0)
						message sendMessage;
						sendMessage.opcode = sendingData;

						if (uploadFiles[index]->nLeft > BUFF_SIZE)
						{
							for (int i = 0; i < BUFF_SIZE; i++) {
								sendMessage.payload[i] = uploadFiles[index]->fileBuffer[uploadFiles[index]->idx + i];
							}
							sendMessage.length = BUFF_SIZE;
						}
						else
						{
							for (int i = 0; i < uploadFiles[index]->nLeft; i++) {
								sendMessage.payload[i] = uploadFiles[index]->fileBuffer[uploadFiles[index]->idx + i];
							}
							sendMessage.length = uploadFiles[index]->nLeft;
						}
						sendMessage.offset = uploadFiles[index]->idx;
						memcpy(sockInfo->buff, &sendMessage, sizeof(message));

						ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
						sockInfo->sentBytes = 0;
						sockInfo->dataBuff.buf = sockInfo->buff;
						sockInfo->dataBuff.len = sizeof(message);
						sockInfo->operation = SEND;
						if (WSASend(sockInfo->sockfd,
							&(sockInfo->dataBuff),
							1,
							&sendBytes,
							0,
							&(sockInfo->overlapped),
							workerUploadRoutine) == SOCKET_ERROR) {
							if (WSAGetLastError() != WSA_IO_PENDING) {
								printf("WSASend() failed with error %d\n", WSAGetLastError());
								return;
							}
						}
						uploadFiles[index]->nLeft -= sendMessage.length;
						uploadFiles[index]->idx += sendMessage.length;
					}
					else if (uploadFiles[index]->nLeft == 0)
					{   // if sent all data in file 
						// post WSASent to send the message with length=0
						// to annouce that this is the last message
						message sendMessage;
						sendMessage.opcode = sendingData;
						sendMessage.payload[0] = 0;
						sendMessage.length = 0;
						memcpy(sockInfo->buff, &sendMessage, sizeof(message));//???
						ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
						sockInfo->sentBytes = 0;
						sockInfo->dataBuff.buf = sockInfo->buff;
						sockInfo->dataBuff.len = sizeof(message);
						sockInfo->operation = SEND;
						if (WSASend(sockInfo->sockfd,
							&(sockInfo->dataBuff),
							1,
							&sendBytes,
							0,
							&(sockInfo->overlapped),
							workerUploadRoutine) == SOCKET_ERROR) {
							if (WSAGetLastError() != WSA_IO_PENDING) {
								printf("WSASend() failed with error %d\n", WSAGetLastError());
								return;
							}
						}
					}
				}
				else if (sendMessage->opcode == md5Code)
				{
					message sendMessage;
					sendMessage.opcode = sendingData;
					if (uploadFiles[index]->nLeft > BUFF_SIZE)
					{
						for (int i = 0; i < BUFF_SIZE; i++) {
							sendMessage.payload[i] = uploadFiles[index]->fileBuffer[uploadFiles[index]->idx + i];
						}
						sendMessage.length = BUFF_SIZE;
					}
					else
					{
						for (int i = 0; i < uploadFiles[index]->nLeft; i++) {
							sendMessage.payload[i] = uploadFiles[index]->fileBuffer[uploadFiles[index]->idx + i];
						}
						sendMessage.length = uploadFiles[index]->nLeft;
					}
					sendMessage.offset = uploadFiles[index]->idx;

					memcpy(sockInfo->buff, &sendMessage, sizeof(message));

					//Third: begin to sending data of file to server
					ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
					sockInfo->sentBytes = 0;
					sockInfo->dataBuff.buf = sockInfo->buff;
					sockInfo->dataBuff.len = sizeof(message);
					sockInfo->operation = SEND;
					if (WSASend(sockInfo->sockfd,
						&(sockInfo->dataBuff),
						1,
						&sendBytes,
						0,
						&(sockInfo->overlapped),
						workerUploadRoutine) == SOCKET_ERROR) {
						if (WSAGetLastError() != WSA_IO_PENDING) {
							printf("WSASend() failed with error %d\n", WSAGetLastError());
							return;
						}
					}
					uploadFiles[index]->nLeft -= sendMessage.length;
					uploadFiles[index]->idx += sendMessage.length;
				}
			}
		}
	}
}

unsigned __stdcall workerDownloadThread(LPVOID lpParameter)
{
	DWORD flags;
	WSAEVENT events[1];
	DWORD index;
	DWORD sendBytes;

	events[0] = (WSAEVENT)lpParameter;
	while (TRUE) {
		while (TRUE) {
			index = WSAWaitForMultipleEvents(1, events, FALSE, WSA_INFINITE, TRUE);
			if (index == WSA_WAIT_FAILED) {
				printf("WSAWaitForMultipleEvents() failed with error %d\n", WSAGetLastError());
				return 1;
			}
			if (index != WAIT_IO_COMPLETION) {
				// An accept() call event is ready - break the wait loop
				break;
			}
		}

		WSAResetEvent(events[index - WSA_WAIT_EVENT_0]);

		EnterCriticalSection(&downloadCriticalSection);

		if (nDownloadSockets == MAX_SOCK) {
			printf("Too many Socket.\n");
			closesocket(client);
			continue;
		}
		if ((downloadSockets[nDownloadSockets] = (LPSOCKET_INFORMATION)GlobalAlloc(GPTR, sizeof(SOCKET_INFORMATION))) == NULL) {
			printf("GlobalAlloc() failed with error %d\n", GetLastError());
			return 1;
		}
		if ((downloadFiles[nDownloadSockets] = (LPFILE_INFORMATION)GlobalAlloc(GPTR, sizeof(FILE_INFORMATION))) == NULL) {
			printf("GlobalAlloc() failed with error %d\n", GetLastError());
			return 1;
		}


		strcpy_s(downloadFiles[nDownloadSockets]->fileName, name);

		//Get file length

		message sendMessage;
		sendMessage.opcode = fileNameDownload;

		strcpy_s(sendMessage.payload, downloadFiles[nDownloadSockets]->fileName);
		sendMessage.length = strlen(downloadFiles[nDownloadSockets]->fileName);
		memcpy(downloadSockets[nDownloadSockets]->buff, &sendMessage, sizeof(message));

		// gui message moi vs payload la ten file

		downloadSockets[nDownloadSockets]->sockfd = client;
		ZeroMemory(&downloadSockets[nDownloadSockets]->overlapped, sizeof(WSAOVERLAPPED));
		downloadSockets[nDownloadSockets]->sentBytes = 0;
		downloadSockets[nDownloadSockets]->recvBytes = 0;
		downloadSockets[nDownloadSockets]->dataBuff.len = sizeof(message);
		downloadSockets[nDownloadSockets]->dataBuff.buf = downloadSockets[nDownloadSockets]->buff;
		downloadSockets[nDownloadSockets]->operation = SEND;
		flags = 0;

		if (WSASend(downloadSockets[nDownloadSockets]->sockfd,
			&(downloadSockets[nDownloadSockets]->dataBuff),
			1,
			&sendBytes,
			0,
			&(downloadSockets[nDownloadSockets]->overlapped),
			workerDownloadRoutine) == SOCKET_ERROR) {
			if (WSAGetLastError() != WSA_IO_PENDING) {
				printf("WSASend() failed with error %d\n", WSAGetLastError());
				return 1;
			}
		}
		printf("\nSocket %d got connected...\n", client);
		nDownloadSockets++;
		LeaveCriticalSection(&downloadCriticalSection);
	}

	return 0;
}

void CALLBACK workerDownloadRoutine(DWORD error, DWORD transferredBytes, LPWSAOVERLAPPED overlapped, DWORD inFlags)
{
	DWORD sendBytes, recvBytes;
	DWORD Flags;

	LPSOCKET_INFORMATION sockInfo = (LPSOCKET_INFORMATION)overlapped;

	if (error != 0)
		printf("I/O operation failed with error %d\n", error);

	if (transferredBytes == 0)
		printf("Closing socket %d\n\n", sockInfo->sockfd);

	if (error != 0 || transferredBytes == 0) {
		//Find and remove socket
		EnterCriticalSection(&downloadCriticalSection);

		int index;
		for (index = 0; index < nDownloadSockets; index++)
			if (downloadSockets[index]->sockfd == sockInfo->sockfd)
				break;

		closesocket(downloadSockets[index]->sockfd);
		GlobalFree(downloadSockets[index]);
		GlobalFree(downloadFiles[index]);

		for (int i = index; i < nDownloadSockets - 1; i++)
		{
			downloadSockets[i] = downloadSockets[i + 1];
			downloadFiles[i] = downloadFiles[i + 1];
		}
		nDownloadSockets--;

		LeaveCriticalSection(&downloadCriticalSection);

		return;
	}

	int index;
	for (index = 0; index < nDownloadSockets; index++)
		if (downloadSockets[index]->sockfd == sockInfo->sockfd)
			break;

	if (sockInfo->operation == SEND) {
		sockInfo->sentBytes += transferredBytes;
		sockInfo->recvBytes = 0;
	}
	else if (sockInfo->operation == RECEIVE) {
		sockInfo->recvBytes += transferredBytes;
		sockInfo->sentBytes = 0;

	}

	if (sockInfo->recvBytes > sockInfo->sentBytes)
	{// after receive from server
		if (sockInfo->recvBytes < sizeof(message))
		{// if receive bytes is less than message size
		 // post another WSARecv
			ZeroMemory(&(sockInfo->overlapped), sizeof(OVERLAPPED));
			sockInfo->dataBuff.buf = sockInfo->buff + sockInfo->sentBytes;
			sockInfo->dataBuff.len = sizeof(message) - sockInfo->recvBytes;
			sockInfo->operation = RECEIVE;

			if (WSARecv(sockInfo->sockfd,
				&(sockInfo->dataBuff),
				1,
				&transferredBytes,
				0,
				&(sockInfo->overlapped),
				workerDownloadRoutine) == SOCKET_ERROR) {
				if (WSAGetLastError() != ERROR_IO_PENDING) {
					printf("WSARecv1() failed with error %d\n", WSAGetLastError());
					return;
				}
			}
		}
		else if (sockInfo->recvBytes == sizeof(message))
		{// if receive message to annouce result from server
		 // process the information
			message  *recvMessage;
			recvMessage = (message *)sockInfo->dataBuff.buf;
			if (recvMessage->opcode == sendingData)
			{
				if (recvMessage->length == 0)
				{//nhan duoc goi tin cuoi cung
				 // message from server to annouce that
				 // file has been upload successfully

					
					fclose(downloadFiles[index]->file);
					EnterCriticalSection(&downloadCriticalSection);

					int index;
					for (index = 0; index < nDownloadSockets; index++)
						if (downloadSockets[index]->sockfd == sockInfo->sockfd)
							break;
					MD5 md5;
					if (strcmp(downloadFiles[index]->digest, md5.digestFile(downloadFiles[index]->fileName)) == 0) 
					{
						printf("Closing socket %d\n", downloadSockets[index]->sockfd);
						closesocket(downloadSockets[index]->sockfd);
						GlobalFree(downloadSockets[index]);
						GlobalFree(downloadFiles[index]);
						printf("File store succesfull in debug folder \n");
					}
					else
					{
						printf("Closing socket %d\n because file %s is corrupted", downloadSockets[index]->sockfd,downloadFiles[index]->fileName);
						closesocket(downloadSockets[index]->sockfd);
						GlobalFree(downloadSockets[index]);
						printf("Begin to download file again");
						downloadFileFromServer(downloadFiles[index]->fileName);
						GlobalFree(downloadFiles[index]);
					}
					

					for (int i = index; i < nDownloadSockets - 1; i++)
					{
						downloadSockets[i] = downloadSockets[i + 1];
						downloadFiles[i] = downloadFiles[i + 1];
					}
					nDownloadSockets--;

					LeaveCriticalSection(&downloadCriticalSection);
					
				}
				else if (recvMessage->length > 0)
				{
					fseek(downloadFiles[index]->file, recvMessage->offset, SEEK_SET);
					fwrite(recvMessage->payload, 1, recvMessage->length, downloadFiles[index]->file);

					// continue to post RECV
					ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
					sockInfo->recvBytes = 0;
					sockInfo->sentBytes = 0;
					Flags = 0;
					sockInfo->dataBuff.len = sizeof(message);
					sockInfo->dataBuff.buf = sockInfo->buff;
					sockInfo->operation = RECEIVE;
					if (WSARecv(sockInfo->sockfd,
						&(sockInfo->dataBuff),
						1,
						&recvBytes,
						&Flags,
						&(sockInfo->overlapped),
						workerDownloadRoutine) == SOCKET_ERROR) {
						if (WSAGetLastError() != WSA_IO_PENDING) {
							printf("WSARecv() failed with error %d\n", WSAGetLastError());
							return;
						}
					}
				}
			}
			else if (recvMessage->opcode == md5Code)
			{
				printf("checking");


				downloadFiles[index]->file = fopen(downloadFiles[index]->fileName, "wb");
				if (!downloadFiles[index]->file)
				{
					fprintf(stderr, "Unable to open file %s", downloadFiles[index]->fileName);
					return;
				}

				strcpy_s(downloadFiles[index]->digest, recvMessage->payload);

				message sendMessage;
				sendMessage.opcode = downloadFile;
				sendMessage.payload[0] = 0;
				sendMessage.length = 0;
				memcpy(sockInfo->buff, &sendMessage, sizeof(message));//???
				ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
				sockInfo->sentBytes = 0;
				sockInfo->dataBuff.buf = sockInfo->buff;
				sockInfo->dataBuff.len = sizeof(message);
				sockInfo->operation = SEND;
				if (WSASend(sockInfo->sockfd,
					&(sockInfo->dataBuff),
					1,
					&sendBytes,
					0,
					&(sockInfo->overlapped),
					workerDownloadRoutine) == SOCKET_ERROR) {
					if (WSAGetLastError() != WSA_IO_PENDING) {
						printf("WSASend() failed with error %d\n", WSAGetLastError());
						return;
					}
				}
			}
			else if (recvMessage->opcode == noFileExist)
			{
				// message from server to annouce that
				// file is existing on server
				EnterCriticalSection(&downloadCriticalSection);

				int index;
				for (index = 0; index < nDownloadSockets; index++)
					if (downloadSockets[index]->sockfd == sockInfo->sockfd)
						break;
				printf("Closing socket %d\n", downloadSockets[index]->sockfd);
				closesocket(downloadSockets[index]->sockfd);
				GlobalFree(downloadSockets[index]);
				GlobalFree(downloadFiles[index]);

				for (int i = index; i < nDownloadSockets - 1; i++)
				{
					downloadSockets[i] = downloadSockets[i + 1];
					downloadFiles[i] = downloadFiles[i + 1];
				}
				nDownloadSockets--;

				LeaveCriticalSection(&downloadCriticalSection);
				printf("File doesnt existed  on server ");
			}
		}
	}
	else
	{// after sending to server
		if (sockInfo->sentBytes < sizeof(message))
		{   // if sent bytes is less than message size
			// post another WSASend

			ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
			sockInfo->dataBuff.buf = sockInfo->buff + sockInfo->sentBytes;
			sockInfo->dataBuff.len = sizeof(message) - sockInfo->sentBytes;
			sockInfo->operation = SEND;
			if (WSASend(sockInfo->sockfd,
				&(sockInfo->dataBuff),
				1,
				&sendBytes,
				0,
				&(sockInfo->overlapped),
				workerDownloadRoutine) == SOCKET_ERROR) {
				if (WSAGetLastError() != WSA_IO_PENDING) {
					printf("WSASend() failed with error %d\n", WSAGetLastError());
					return;
				}
			}
		}
		else if (sockInfo->sentBytes == sizeof(message))
		{// after sent bytes equal to message size
			message  *sendMessage;
			sendMessage = (message *)sockInfo->dataBuff.buf;
			if (sendMessage->opcode == fileNameDownload)
			{
				//Second: after sending file name to server
				// post WSARecv to confirm file is existing on server or not
				ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
				sockInfo->recvBytes = 0;
				sockInfo->sentBytes = 0;
				Flags = 0;
				sockInfo->dataBuff.len = sizeof(message);
				sockInfo->dataBuff.buf = sockInfo->buff;
				sockInfo->operation = RECEIVE;
				if (WSARecv(sockInfo->sockfd,
					&(sockInfo->dataBuff),
					1,
					&recvBytes,
					&Flags,
					&(sockInfo->overlapped),
					workerDownloadRoutine) == SOCKET_ERROR) {
					if (WSAGetLastError() != WSA_IO_PENDING) {
						printf("WSARecv() failed with error %d\n", WSAGetLastError());
						return;
					}
				}
			}
			else if (sendMessage->opcode == downloadFile)
			{
				ZeroMemory(&(sockInfo->overlapped), sizeof(WSAOVERLAPPED));
				sockInfo->recvBytes = 0;
				sockInfo->sentBytes = 0;
				Flags = 0;
				sockInfo->dataBuff.len = sizeof(message);
				sockInfo->dataBuff.buf = sockInfo->buff;
				sockInfo->operation = RECEIVE;
				if (WSARecv(sockInfo->sockfd,
					&(sockInfo->dataBuff),
					1,
					&recvBytes,
					&Flags,
					&(sockInfo->overlapped),
					workerDownloadRoutine) == SOCKET_ERROR) {
					if (WSAGetLastError() != WSA_IO_PENDING) {
						printf("WSARecv() failed with error %d\n", WSAGetLastError());
						return;
					}
				}
			}
		}
	}
}


