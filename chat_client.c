#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), sendto(), and recvfrom() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <sys/mman.h>
#include <poll.h>


#define MAXPENDING 5    /*max pending connection requests*/

void DieWithError(char *errorMessage);  /* External error handling function */
//TODO: write readme file

int main(int argc, char *argv[]) {
    int sock;                        /* Socket descriptor */
    struct sockaddr_in echoServAddr; /* Echo server address */
    struct sockaddr_in fromAddr;     /* Source address of echo */
    unsigned short echoServPort;     /* Echo server port */
    unsigned int fromSize;           /* In-out of address size for recvfrom() */
    char *servIP;                    /* IP address of server */
    int respStringLen;               /* Length of received response */
    char sendBuffer[200];
    char responseBuffer[200];
    char userID[5];
    int menuChoice = 0;                   /*client menu choice to send who or talk request to server*/
    char ch; //for clearing input buffer

    /*-----------------------TCP RELATED VARIABLES----------------------------------------------*/
    int handleTcpSocket;
    int sendingTcpSocket;
    int listenTcpSocket;
    struct sockaddr_in tcpServAddr;
    struct sockaddr_in yourTcpAddress;
    struct sockaddr_in incomingTcpAddr;
    unsigned short tcpPort = 25873;    /*tcp port for communication between clients*/
    unsigned int incomingAddrLength;                /*length of client address data structure*/


    enum msgType {
        login,
        whoRequest,
        talkRequest,
        logout
    };

    enum tcpMsgType {
        OK,
        decline,
        endChat
    };

    //struct for messages; contain msg type, the message, and a number field for any numerical data
    typedef struct msg {
        int msgType;
        char msg[100];
        unsigned int numField;
    } msg;

    //struct for tcp messages sent between users, contains sender's userID and their msg
    typedef struct tcpMsg {
        int tcpMsgType;
        char userID[5];
        char msg[200];
    } tcpStruct;

    /* Test for correct number of arguments */
    if ((argc < 2) || (argc > 3)) {
        fprintf(stderr, "Usage: %s <Server IP> [<Echo Port>]\n", argv[0]);
        exit(1);
    }

    /*------------------------------------SERVER/SOCKET SETUP-----------------------------------------------------*/
    /********************************UDP*******************************/
    servIP = argv[1];           /* First arg: server IP address (dotted quad) */

    if (argc == 3)
        echoServPort = atoi(argv[2]);  /* Use given port, if any */
    else
        echoServPort = 7;  /* 7 is the well-known port for the echo service */

    /* Create a datagram/UDP socket */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        DieWithError("socket() failed");

    /* Construct the server address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));    /* Zero out structure */
    echoServAddr.sin_family = AF_INET;                 /* Internet addr family */
    echoServAddr.sin_addr.s_addr = inet_addr(servIP);  /* Server IP address */
    echoServAddr.sin_port = htons(echoServPort);     /* Server port */

    /*****************************Incoming TCP*******************************/

    /* Create socket for incoming connections */
    if ((listenTcpSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        DieWithError("listening tcp socket creation failed");

    //create incoming (tcpservaddr) and outgoing(yourtcpaddress) tcp address structures
    memset(&tcpServAddr, 0, sizeof(tcpServAddr));   /* Zero out structure */
    memset(&yourTcpAddress, 0, sizeof(yourTcpAddress));
    tcpServAddr.sin_family = AF_INET;                /* Internet address family */
    tcpServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    tcpServAddr.sin_port = htons(tcpPort);      /* Local port */

    //assign af_inet to tcp address structure
    yourTcpAddress.sin_family = AF_INET;


    /* Bind tcp socket to local address */
    if (bind(listenTcpSocket, (struct sockaddr *) &tcpServAddr, sizeof(tcpServAddr)) < 0)
        DieWithError("bind() failed");


    /*------------------------------------END SOCKET/SERVER SETUP-----------------------------------------*/

    //prompt user to enter userID to login
    printf("Welcome to Chat Client! Enter your four-digit ID to log in\n");
    fgets(userID, 5, stdin);
    ch = getchar();

    printf("requested user ID: %s\n", userID);

    //create loginMsg struct to send to server
    msg loginMsg;
    loginMsg.msgType = login;
    loginMsg.numField = tcpPort;
    strcpy(loginMsg.msg, userID);

    /* Send the userID to the server */
    if (sendto(sock, &loginMsg, sizeof(loginMsg), 0, (struct sockaddr *)
            &echoServAddr, sizeof(echoServAddr)) != sizeof(loginMsg))
        DieWithError("sendto() sent a different number of bytes than expected");
    else {
        printf("requested ID and your tcp port sent to server: %s %u\n", loginMsg.msg, loginMsg.numField);
    }

    //receive response after sending login message
    msg request;
    msg response;
    memset(&response, 0, sizeof(response));
    fromSize = sizeof(fromAddr);
    if ((respStringLen = recvfrom(sock, &response, sizeof(response), 0,
                                  (struct sockaddr *) &fromAddr, &fromSize)) < 0)
        DieWithError("recvfrom() failed");
    else printf("%s\n", response.msg);

    if (echoServAddr.sin_addr.s_addr != fromAddr.sin_addr.s_addr) {
        fprintf(stderr, "Error: received a packet from unknown source.\n");
        exit(1);
    }


    /*
     * msgCheck is shared between parent and child processes. When it is set to 1, it means there is a message to
     * respond to
     */
    int *connected = 0; //0 means not currently connected to anyone, 1 means you are
    int *menuCount = 0; //when 0, menu is displayed. when 1, not displayed
    int *initiator = 0; // used to toggle input on/off in parent process
    int rdyMsg = 0; //control if a message can be sent to server
    menuCount = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED| MAP_ANON, -1, 0);
    connected = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED| MAP_ANON, -1, 0);
    initiator = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED| MAP_ANON, -1, 0);


    /* Begin menu operations------------------------------------------------------------------------- */
    if (fork() > 0) {
        *initiator = 1;

        //poll struct for constantly looping and checking if a message has been received
        struct pollfd mypoll = { STDIN_FILENO, POLLIN|POLLPRI};

        for (;;) {
            //if not connected, menu options are displayed
            if (*connected == 0 && *initiator == 1) {
                rdyMsg = 0;

                //clear message buffer between messages
                memset(&request, 0, sizeof(request));
                memset(&response, 0, sizeof(response));
                if (*menuCount == 0)
                    printf("Select an option to continue!\n1-Who\n2-Talk\n3-Quit\n");
                if (poll(&mypoll, 1, 1000)) {
                    scanf("%d", &menuChoice);
                    ch = getchar();
                } else {
                    *menuCount = 1;
                    continue;
                }

                //'who' request---------------------------------------------------------
                if (menuChoice == 1) {
                    request.msgType = whoRequest;
                    strcpy(request.msg, "user requests active user directory");
                    request.numField = 0;
                    *menuCount = 0;
                    rdyMsg = 1;
                }

                //talk request-----------------------------------------------------------
                if (menuChoice == 2) {
                    char contactID[5];
                    printf("Enter user ID you wish to connect to\n");
                    fgets(contactID, 5, stdin);
                    ch = getchar();
                    printf("you selected: %s\n", contactID);
                    request.msgType = talkRequest;
                    strcpy(request.msg, contactID);
                    request.numField = 0;
                    *menuCount = 0;
                    rdyMsg = 1;
                }

                //logout request-----------------------------------------------------------
                if(menuChoice == 3) {
                    request.msgType = logout;
                    strcpy(request.msg, userID);
                    request.numField = 0;
                    *menuCount = 0;
                    rdyMsg = 1;
                }

                //send menu choice to server----------------------------------------------------------------------------
                if(rdyMsg == 1) {
                    if (sendto(sock, &request, sizeof(request), 0, (struct sockaddr *)
                            &echoServAddr, sizeof(echoServAddr)) != sizeof(request))
                        DieWithError("sendto() sent a different number of bytes than expected");
                    else printf("TO SERVER: %s\n", request.msg);
                }

                //receive response from server after menu choice--------------------------------------------------------
                fromSize = sizeof(fromAddr);
                if ((respStringLen = recvfrom(sock, &response, sizeof(response), 0,
                                              (struct sockaddr *) &fromAddr, &fromSize)) < 0)
                    DieWithError("recvfrom() failed");
                else printf("FROM SERVER:");

                if (echoServAddr.sin_addr.s_addr != fromAddr.sin_addr.s_addr) {
                    fprintf(stderr, "Error: received a packet from unknown source.\n");
                    exit(1);
                }

                //processing responses from server----------------------------------------------------------------------

                //prints user directory received from server
                if (response.msgType == whoRequest) {
                    printf("User Directory: %s\n", response.msg);
                }

                //logout request was successsful
                if(response.msgType == logout) {
                    printf("you have been successfully logged out, goodbye!\n");
                    exit(0);
                }
            }

            /*talk request, send tcp connection request to specified user
            if not connected to a user yet, have to request their info
            otherwise can continue to send them messages*/

            if (response.msgType == talkRequest) {
                tcpStruct tcpSend;
                tcpStruct tcpRecv;
                memset(&tcpSend, 0, sizeof(tcpSend));
                memset(&tcpRecv, 0, sizeof(tcpSend));
                if (*connected == 0) {
                    if ((sendingTcpSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
                        DieWithError("sending tcp socket creation failed");
                    printf("requested user ip address and tcp port: %s %u\n", response.msg, response.numField);
                    yourTcpAddress.sin_addr.s_addr = inet_addr(response.msg);
                    yourTcpAddress.sin_port = htons(response.numField);

                    if (connect(sendingTcpSocket, (struct sockaddr *) &yourTcpAddress, sizeof(yourTcpAddress)) < 0) {
                        DieWithError("could not connect to user");
                    } else {
                        recv(sendingTcpSocket, &tcpRecv, sizeof(tcpRecv), 0);
                        if (tcpRecv.tcpMsgType == decline) {
                            close(sendingTcpSocket);
                            printf("User declined connection, returning to menu\n");
                            *connected = 0;
                            *menuCount = 0;
                            *initiator = 1;
                            memset(&response, 0, sizeof(response));
                        } else {
                            printf("you are connected, you may begin chatting\n");
                            *connected = 1;
                        }
                    }
                }

                int responseSize = 0;
                if (*connected == 1) {
                    for (;;) {
                        memset(&tcpSend, 0, sizeof(tcpSend));
                        memset(&tcpRecv, 0, sizeof(tcpRecv));
                        memset(responseBuffer, 0, sizeof(responseBuffer));

                        //get input for message to send, place it and outgoing user ID in struct
                        fgets(tcpSend.msg, 200, stdin);
                        strcpy(tcpSend.userID, userID);

                        if (tcpSend.msg[0] == '\\' && tcpSend.msg[1] == '0') {
                            tcpSend.tcpMsgType = endChat;
                        }
                        else tcpSend.tcpMsgType = OK;

                        //send message entered by user
                        if (send(sendingTcpSocket, &tcpSend, sizeof(tcpSend), 0) != sizeof(tcpSend))
                            DieWithError("send() sent a different number of bytes than expected");
                        else {
                            printf("YOU(%s): %s", userID, tcpSend.msg);
                        }

                        if (tcpSend.tcpMsgType == endChat) {
                            close(sendingTcpSocket);
                            *connected = 0;
                            *menuCount = 0;
                            *initiator = 1;
                            break;
                        }

                        if ((responseSize = recv(sendingTcpSocket, &tcpRecv, sizeof(tcpRecv), 0)) <= 0) {
                            DieWithError("recv() failed or connection closed");
                        }

                        printf("%s:  %s\n", tcpRecv.userID, tcpRecv.msg);
                        if(tcpRecv.tcpMsgType == endChat) {
                            close(sendingTcpSocket);
                            *connected = 0;
                            *menuCount = 0;
                            *initiator = 1;
                            break;
                        }
                    }
                }//end of connected send and receive processes
            }//end of talk request handling, when initiated by user
        } //end parent process for loop
    }

        //forked child process - used for listening, accepting, and processing tcp conversations when not the initiator
    else {
        for (;;) {
            if (listen(listenTcpSocket, MAXPENDING) < 0) {
                DieWithError("listen() failed");
            } else printf("\nYou're logged in, currently listening for connection requests!\n");

            tcpStruct tcpSend;
            tcpStruct tcpRecv;

            for (;;) {
                /* process incoming connections/displaying messages*/
                memset(&tcpSend, 0, sizeof(tcpSend));
                memset(&tcpRecv, 0, sizeof(tcpRecv));
                char prompt[2]; //user input for accepting or rejecting connect request

                //begin determining if connection is accepted
                if (*connected == 0) {
                    incomingAddrLength = sizeof(incomingTcpAddr);
                    printf("waiting for incoming request");
                    if ((handleTcpSocket = accept(listenTcpSocket, (struct sockaddr *) &incomingTcpAddr,
                                                  &incomingAddrLength)) < 0) {
                        DieWithError("accept() failed");
                    } else {
                        *initiator = 0;
                        printf("\nIncoming chat request from client %s, accept? (Y/N)\n",
                               inet_ntoa(incomingTcpAddr.sin_addr));
                        fgets(prompt, 2, stdin);
                        ch = getchar();

                        if (strcmp(prompt, "Y" ) == 0|| strcmp(prompt, "y" )== 0) {
                            printf("You are connected, partner messages first\n");
                            tcpSend.tcpMsgType = OK;
                            send(handleTcpSocket, &tcpSend, sizeof(tcpSend), 0);
                            *connected = 1;
                        } else {
                            tcpSend.tcpMsgType = decline;
                            strcpy(tcpSend.msg, " ");
                            if (send(handleTcpSocket, &tcpSend, sizeof(tcpSend), 0) != sizeof(tcpSend))
                                DieWithError("send() failed");
                            else printf("connection declined, how rude\n");
                            close(handleTcpSocket);
                            *connected = 0;
                            *menuCount = 0;
                            *initiator = 1;
                            continue;
                        }
                    }//end block that is entered when a connect request is received
                }//end processing events to determine if connection is accepted or not

                //can receive/display messages once connected
                if (*connected == 1) {
                    for (;;) {
                        memset(&tcpSend, 0, sizeof(tcpSend));
                        memset(&tcpRecv, 0, sizeof(tcpRecv));

                        int receiveSize;
                        if ((receiveSize = recv(handleTcpSocket, &tcpRecv, sizeof(tcpRecv), 0)) < 0) {
                            printf("error receiving message");
                        } else {
                            printf("user %s : %s", tcpRecv.userID, tcpRecv.msg);
                            if (tcpRecv.tcpMsgType == endChat) {
                                close(handleTcpSocket);
                                printf("connection closed, returning to menu\n");
                                *connected = 0;
                                *menuCount = 0;
                                *initiator = 1;
                                break;
                            }

                            fgets(tcpSend.msg, 200, stdin);
                            strcpy(tcpSend.userID, userID);

                            if (tcpSend.msg[0] == '\\' && tcpSend.msg[1] == '0') {
                                tcpSend.tcpMsgType = endChat;
                            } else tcpSend.tcpMsgType = OK;

                            if (send(handleTcpSocket, &tcpSend, sizeof(tcpSend), 0) != sizeof(tcpSend))
                                DieWithError("send() failed");
                            else printf("YOU(%s): %s\n", tcpSend.userID, tcpSend.msg);
                        }

                        if (tcpSend.tcpMsgType == endChat) {
                            close(sendingTcpSocket);
                            *connected = 0;
                            *menuCount = 0;
                            *initiator = 1;
                            break;
                        }


                    }//end receive/send message loop with initiating user
                }//end of processing events while connected as non-initiator of chat
            }//end child process loop (listening for connections, accepting, chatting)
        }//end of child process
    }

    close(sock);
    exit(0);
}
