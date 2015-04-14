//-----------------------------------------------------------------------
// comms_server_final.c
// Deirdre Reilly
// ME-4 2015
// Final code release 10/04/2015
//
// comms_server_final acts as a communications router between various user programs and
// the main robogo controller
// Unix queues are used to route the messages to robogo and receive alerts back from the
// controller for display on the user interface
// The program was designed to cater for Web interfaces but a 'Mobile First' strategy was
// employed and a mobile application was deploy first.
//
//-----------------------------------------------------------------------

#include <stdio.h>

#include <stdlib.h>

#include <sys/socket.h>

#include <sys/types.h>

#include <sys/ipc.h>

#include <sys/msg.h>

#include <netinet/in.h>

#include <string.h>

#include <unistd.h>

#include <errno.h>


#define SERVER_PORT 12345

#define MAXSIZE     128

void die(char *s)
{
  perror(s);
  exit(1);
}

typedef struct msgbuf   //message buffer construct for the message queue
{
    long    mtype;
    char    mtext[MAXSIZE];
} ;

struct sockaddr_in addr;


//----
// Run with a number of incoming connections as an argument */
//---

int main(int argc, char *argv[])

    {

    int i, len, num, rc,last_error;

    int listen_sd, accept_sd;
    int light_status =0;

    /* Buffer for data coming from and to the sockets*/

    char buffer[100];
    char command[100];

    char lcol[20], ccol[20], rcol[20];  // commands to send the NetIO interface if there is a collision

//----
//    set up the queue to receive messages from the RoBoGo controller
//----    

    int msqid_in;
    int return_status;
    key_t key;
    struct msgbuf rcvbuffer;
	int msgflg = IPC_CREAT | 0666;
    key = 4321;		//identifier for the output queue

    if ((msqid_in = msgget(key, msgflg)) < 0)
      die("msgget()");

//----
//  set up the message queue to talk to the robogo
//----

	int msqid_out;
	msgflg = IPC_CREAT | 0666;
	struct msgbuf sbuf;
	size_t buflen;
    key = 1234;		//identifier for input queue
    if ((msqid_out = msgget(key, msgflg )) < 0)   //Get the message queue ID for the given key
      die("msgget");

    //Message Type
    sbuf.mtype = 1;


    num = 1; // number of network connections from various controllers (Could have web page drive this as well)



//----
//  Create an AF_INET stream socket to receive incoming connections / Standard TCP-IP
//----


    listen_sd = socket(AF_INET, SOCK_STREAM, 0);

    if(listen_sd < 0)

    {

    perror("Iserver - socket() error");

    exit(-1);

    }

    else

    printf("Iserver - socket() is OK\n");



    printf("Binding the socket...\n");

//----
//  Bind to the socket and tell the socket to listen to PORT 12345 from which the NetIO application talks
//----
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;

    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    addr.sin_port = htons(SERVER_PORT);

    rc = bind(listen_sd, (struct sockaddr *)&addr, sizeof(addr));

    if(rc < 0)

    {

    perror("Iserver - bind() error");

    close(listen_sd);

    exit(-1);

    }

    else

    printf("Iserver - bind() is OK\n");


//----
// Set the listen backlog to 5 connections
//----

    rc = listen(listen_sd, 5);

    if(rc < 0)

    {

    perror("Iserver - listen() error");

    close(listen_sd);

    exit(-1);

    }

    else

    printf("Iserver - listen() is OK\n");


//----
//   Inform the user that the server is ready
//----

    printf("The Iserver is ready!\n");

    /* Go through the loop once for each connection */
    /* here is where multiple controlling connections to the robogo code could be accepted
     * we are just using one now for simplicity
     */

    // for(i=0; i < num; i++)

    // {

//----
//  Wait for an incoming connection
//---

    printf(" waiting on accept()\n");

//----
//  Now I'm ready to pick up the 'phone' for the first user to call to control the robogo code
//  then accept the call and make note of the file handle i am given
//----

    accept_sd = accept(listen_sd, NULL, NULL);

    if(accept_sd < 0)

    {

    perror("Iserver - accept() error");

    close(listen_sd);

    exit(-1);

    }

    else

    printf("accept() is OK and completed successfully!\n");

//----
//  Set the reading socket to timeout after 1 second so we can check if there are messages to send to the controller */
//----



    const struct timeval sock_timeout={.tv_sec=1, .tv_usec=0}; // set timeout time on read from socket

    setsockopt(accept_sd, SOL_SOCKET, SO_RCVTIMEO, (char*)&sock_timeout, sizeof(sock_timeout));



//----
//  receive a message from the client
//----

    printf("I am waiting for client to send message(s) to me...\n");

    buffer[0] = 0;
    command[0] = 0;

while (1==1)
    {

//----
// Read the IP socket and time out after 1 second if there is no message from Netio controller
//----

    rc = recv(accept_sd, buffer, sizeof(buffer), 0);
    last_error = errno;	//trap the error message so i can decide what to do

    //If error message is 'I would have blocked' then move ahead as that is the error I am expecting otherwise exit

    if ((rc <= 0) && (last_error != EWOULDBLOCK))

    {

    perror("Iserver - recv() error ");

    close(listen_sd);

    close(accept_sd);

    exit(-1);

    }

    else
    {
        if (rc>0){  // I received some characters from the iOS application so process (else I did not an need to chedk if I have anything to send)


            printf("In the read processing loop%s,%s RC [%d] \n", buffer, command,rc);

            i=0;

            while(*(buffer+i) != '!') //move all the characters up to the extra ! we added in the iOS controller
            {
                sbuf.mtext[i]= buffer[i];
                i++;
            }

            sbuf.mtext[i]=(int)0; // force a null into the end of the string

            printf("The message from client: \"%s\"\n", sbuf.mtext);

//----
//    Echo the data back to the client application as this is what Netio wants
//----

            printf("Echoing it back to client...\n");

            len = rc;

            rc = send(accept_sd, buffer, len, 0);

            if(rc <= 0)

            {

            perror("Iserver - send() error");

            close(listen_sd);

            close(accept_sd);

            exit(-1);

            }

            else

            printf("Iserver - send() is OK.\n");

//----
//  Now send the message onto the message queue for robogo to process
//----

            buflen = strlen(sbuf.mtext)+1 ;

            printf("about to send\n");
            printf("\n");

               if (msgsnd(msqid_out, &sbuf, buflen, IPC_NOWAIT) < 0)
               {
                   printf ("%d, %d, %s, %d\n", msqid_out, sbuf.mtype, sbuf.mtext, buflen);
                   die("msgsnd");
               }

               else {
                   printf("Message Sent\n");
               	if (strcmp(sbuf.mtext,"exit")==0) break;
           }


          buffer[0] = 0;
          command[0] = 0;


        }
//----
//  WE HAVE FINISHED SENDING FROM USER TO RoBoGo NOW CHECK IF RoBoGo HAS A MESSAGE FOR THE USER
//----

        else  {  // Now I need to check if I have any feedback to send to the iOS application

        rc = msgrcv(msqid_in, &rcvbuffer, MAXSIZE, 1, IPC_NOWAIT);
            last_error = errno;

        if (rc < 0){	// ENOMSG tells us there is nothing in the queue so go back around the while loop again
                if (last_error == ENOMSG) {
                    //printf("Nothing in the Queue\n");
                    }
                else
                die("Error reading from input queue");
                }
        if (rc >=0) {		// we have a message from RoBoGo



            lcol[0]=0;	// strings to hold the individual messges for the three types of collisions
            ccol[0]=0;
            rcol[0]=0;

            sscanf(rcvbuffer.mtext, "%s %s %s", lcol, ccol, rcol); // Pick the collision messages from the returned string


            printf("Read [%s] [%s] [%s] from the robogo queue\n", lcol,ccol,rcol);

            strcat(lcol,"\r"); // Add new lines as Netio needs this to know it has reached the end of a string
            strcat(ccol,"\r");
            strcat(rcol,"\r");
            lcol[strlen(lcol)+1] =(int)0; // make sure we still have a valid string with no junk on the end
            ccol[strlen(ccol)+1] =(int)0;
            rcol[strlen(rcol)+1] =(int)0;

            usleep(10000); // Slow it down so we are not spinning at max CPU speed

//----
//  Send the three individual collision errors to the Netio user interface
//----

            printf("About to send centre [%s]\n",ccol);

            rc = send(accept_sd, ccol, strlen(ccol)+1, 0);    //send the centre status

            if(rc <= 0)

            {

            perror("Iserver - send() error");

            close(listen_sd);

            close(accept_sd);

            exit(-1);

            }

            printf("About to send left [%s]\n",lcol);
            rc = send(accept_sd, lcol, strlen(lcol)+1, 0);    //send the left status

            if(rc <= 0)

            {

            perror("Iserver - send() error");

            close(listen_sd);

            close(accept_sd);

            exit(-1);

            }

            printf("About to send right [%s]\n",rcol);
            rc = send(accept_sd, rcol, strlen(rcol)+1, 0);    //send the right status

            if(rc <= 0)

            {

            perror("Iserver - send() error");

            close(listen_sd);

            close(accept_sd);

            exit(-1);

            }
//----
//  here is where we would send speed if the quadrtiture encoders were working
//----

            else {

            printf("Iserver - send() is OK.\n");
            buffer[0] = 0;

            }


        }

        }
    }
} // end of while loop
    close(listen_sd);

    close(accept_sd);
} // end of main


