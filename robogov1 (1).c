//-----------------------------------------------------------------------
// robogov1.c
// Deirdre Reilly
// ME-4 2015
// Final code release 10/04/2015
//
// Robogo reads messages from the message queue managed by the comms_server
// The messages are processes and equivalent commands sent to the motors
// If no commands are being received then keep checking the ultrasonic sensors 
// and look out for collisions
// 
//-----------------------------------------------------------------------

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <stdlib.h>
#include <sys/select.h>
#include <termios.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>

#define FALSE 0
#define TRUE !(FALSE)

//----
//Global variables
//----


struct msgbuf /*Standard Message buffer format */
{
    long    mtype;
    char    mtext[128];
};

int l_motor_fd, r_motor_fd, u_sensor_fd; //File Descriptorss for motors and ultrasonics
int l_distance, c_distance, r_distance;  //distrance to objects on ultrasoncis sensors
int in_collision = FALSE;                // am I in a collision mode
int DEBUG_ON =1;                         // if 1 show prints, if 0 no prints
int ARD_DEBUG_ON =0;                     // if 1 prints arduino debug trace

/* 
 *  Structures for the message queue
 */


int msqido;
int msgflg = IPC_CREAT | 0666;
key_t okey;
struct msgbuf sbuf; /* the send buffer */
size_t obuflen;

void die(char *s)
{
  perror(s);
  exit(1);
}


//----
// Setup and initialization routines
//----

int setup_interfaces(){
	   struct termios options;


		//---
		// Setup Arduino on /dev/ttyO1 to collect obstacle distance data from the ultrasonics
		//---

		if (DEBUG_ON == TRUE) printf("About to open Arduino port");
		if ((u_sensor_fd = open("/dev/ttyO1", O_RDWR | O_NOCTTY ))<0){
			 perror("UART: Failed to open Arduino on /dev/ttyO1\n");
		      return -1;
		   }
		   tcgetattr(u_sensor_fd, &options);
		   options.c_cflag = B9600 | CS8 | CREAD | CLOCAL;
		   options.c_iflag = IGNPAR | ICRNL;
		   //options.c_cflag &= ~(ICANON | ECHO | ECHOE | ISIG);
		   //options.c_oflag &= ~OPOST;
		   //tcsetattr(u_sensor_fd, TCSANOW, &options);
		   tcflush(u_sensor_fd, TCIFLUSH);    // clear out any junk on the UART buffers

		//---
		// Setup right motor control on /dev/ttyO4
		//---

	if ((r_motor_fd = open("/dev/ttyO4", O_RDWR | O_NOCTTY |O_NDELAY ))<0){
	         perror("UART: Failed to open Right Motor Control on /dev/tty04\n");
	         return -1;
	      }
	   tcgetattr(r_motor_fd, &options);
	    options.c_cflag = B9600 | CS8 | CREAD | CLOCAL;
	    options.c_iflag = IGNPAR | ICRNL;
	    options.c_cflag &= ~(ICANON | ECHO | ECHONL | ISIG | IEXTEN);
	    options.c_oflag &= ~(ONLCR | OCRNL);
	    //   options.c_cc[VMIN]=1;
	    //   options.c_cc[VTIME]=50;
	    // tcsetattr(file, TCSANOW, &options);
	    tcflush(r_motor_fd, TCIFLUSH);

		//---
		// Setup left motor control on /dev/ttyO2
		//---

	if ((l_motor_fd = open("/dev/ttyO2", O_RDWR | O_NOCTTY |O_NDELAY ))<0){
	         perror("UART: Failed to open Left Motor Control on /dev/tty02\n");
	         return -1;
	      }
	   tcgetattr(l_motor_fd, &options);
	    options.c_cflag = B9600 | CS8 | CREAD | CLOCAL;
	    options.c_iflag = IGNPAR | ICRNL;
	    options.c_cflag &= ~(ICANON | ECHO | ECHONL | ISIG | IEXTEN);
	    options.c_oflag &= ~(ONLCR | OCRNL);
	    //   options.c_cc[VMIN]=1;
	    //   options.c_cc[VTIME]=50;
	    // tcsetattr(file, TCSANOW, &options);
	    tcflush(l_motor_fd, TCIFLUSH);

}
//----
//Drive Motor Routine
//----

int drive_motors(char *l_motor_command, char *r_motor_command, int wait_timer_sec) {
	int count; //number of chars sent or read with motors
	unsigned char receive[100];
//----
// Write to left and right motors
//---

	// Write left motor command to FD
	if ((count = write(l_motor_fd, l_motor_command, strlen(l_motor_command)))<0){
     	   perror("Failed to write to Left Motor\n");
            return -1;
        }
	if (DEBUG_ON == TRUE) printf("wrote [%d] chars [%s] to Left Motor\n\r",strlen(l_motor_command),l_motor_command);

	// Write right motor command to FD
	if ((count = write(r_motor_fd, r_motor_command, strlen(r_motor_command)))<0){
     	   perror("Failed to write to Right Motor\n");
            return -1;
        }
	if (DEBUG_ON == TRUE) printf("wrote [%d] chars [%s] to Right Motor\n\r",strlen(r_motor_command),r_motor_command);


//----
// Check response back from motor controllers
//----
    if ((count = read(l_motor_fd, (void*)receive, 100))<0){
       perror("Failed to read from the Left motor\n");
 //      return -1;
    }
    if (count==0) printf("There was no data available to read from Left motor\n");
    else {
       receive[count]=0;  //Put in a null to make a string if needs be
    if (DEBUG_ON == TRUE) printf("[%d]: [%s]",count,receive);
    }

    if ((count = read(r_motor_fd, (void*)receive, 100))<0){
       perror("Failed to read from the Right motor\n");
 //      return -1;
    }
    if (count==0) printf("There was no data available to read from Right Motor\n");
    else {
       receive[count]=0;
    if (DEBUG_ON == TRUE) printf("    [%d]: [%s]\n\r",count,receive);
    }

	if (wait_timer_sec > 0) {
		usleep(wait_timer_sec*1000000); //sleep to allow motors to run for x seconds
		drive_motors("X\r","X\r",0);
	}

}
//----
// Reset_interfaces sends GO GO to the motors, that clears out error conditions and breaker trips
//----

void reset_interfaces(){
	int c;

	c = drive_motors("GO\r","GO\r",0);

}

//----
// Read a series of characters from the Arduino. MSG TYPE L,C,R and then !
// Later can be expanded with other messages types (e.g. speed, acceleration etc)
// Message type 1 is from the ultrasonic sensors
//---

void read_arduino(){

	char receive[100];
	char arduino_string[100];
	int ard_message_type;
	int a,b,c,d;
	int count;
	int i = 0;

	//----
	// Check response back from arduino
	//----
while(1==1){


	if ((count = read(u_sensor_fd, (void*)receive, 1))<0){ //read from the Arduino
	       perror("Failed to read from the Arduino\n");
	 //      return -1;
	    }
	    if (count==0) printf("There was no data available to read from Arduino\n");
	    else {
	       receive[count]=0;  //Put in a null to make a string if needs be
	    if (ARD_DEBUG_ON == TRUE) printf("The following was read in from Arduino [%d]: %s\n\r",count,receive);
	    
        if (receive[0]!=33) { //Read from the Arduino one character at a time until an ! is reached
	    	arduino_string[i] = receive[0];
	    	i++;
	    	}

	    else {
	    	arduino_string[i]=0;
	    	i=0;
	        if (ARD_DEBUG_ON ==TRUE) printf("The combined ARDUINO string is %s\n", arduino_string);
	    sscanf(arduino_string, "%d %d %d %d", &ard_message_type, &a,&b,&c); // extract the distance parameters from the Arduino response
	    if (ard_message_type ==1){
	    		l_distance = a;
	    		c_distance = b;
	    		r_distance = c;
	    	if (ARD_DEBUG_ON == TRUE) printf("%d %d %d %d\n",ard_message_type,l_distance,c_distance,r_distance);
	    		}
	    }
	}
	    if(receive[0]==33) break; // 33 is an !
}

}


//----
// Make decision on what to do in case of a pending collision
//---

void send_collision_msg(char send_msg[100]) {

	if (ARD_DEBUG_ON == TRUE) printf("\n");
	if (ARD_DEBUG_ON == TRUE)printf("Collision string to send is [%s]",send_msg);

	sbuf.mtext[0]=0;
	strcpy (sbuf.mtext,send_msg);
	//sbuf.mtext[strlen(send_msg+2)]=0;
	sbuf.mtype=1;

	if (ARD_DEBUG_ON == TRUE) printf("The collision message about to send is [%s]\n",sbuf.mtext);

    obuflen = strlen(sbuf.mtext)+1;

    if (msgsnd(msqido, &sbuf, obuflen, IPC_NOWAIT) < 0)
    {
        printf ("%d, %d, %s, %d\n", msqido, sbuf.mtype, sbuf.mtext, obuflen);
        die("msgsnd");
    }
    else
    {
    if (ARD_DEBUG_ON == TRUE)   printf("Message Sent\n");
    }
}

//----
// Avoid collision routine 
// Send a message to the user interface turning on the correct alert lamps
// Then send an evasive action to the motors
// 
//----

void avoid_collision() {

	if (DEBUG_ON == TRUE) printf("Collision\n");

	if (c_distance <10) { // centre sensor

		send_collision_msg("<lc:lamp_off> <cc:lamp_on> <rc:lamp_off>");

		drive_motors("R2000\r","R2000\r",2);
	}

	if (l_distance <10) { // left sensor
		send_collision_msg("<lc:lamp_on> <cc:lamp_off> <rc:lamp_off>");
		drive_motors("R2000\r","R2000\r",2);
		drive_motors("F2000\r","R2000\r",2);

	}

	if (r_distance <10) { // right sensor
		send_collision_msg("<lc:lamp_off> <cc:lamp_off> <rc:lamp_on>");
		drive_motors("R2000\r","R2000\r",2);
		drive_motors("R2000\r","F2000\r",2);
	}

	send_collision_msg("<lc:lamp_off> <cc:lamp_off> <rc:lamp_off>"); // finshed evasive action so turn off warning lamps

	return;
}

//----
// Setup the keyboard for single keystroke processing
// These routines are used for debugging the controller RoBoGo program if the message queue is not in use
// 
//---

struct termios orig_termios;

void reset_terminal_mode()
{
    tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode()
{
    struct termios new_termios;

    /* take two copies - one for now, one for later */
    tcgetattr(0, &orig_termios);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));

    /* register cleanup handler, and set the new terminal mode */
    atexit(reset_terminal_mode);
    cfmakeraw(&new_termios);
    tcsetattr(0,TCSANOW, &new_termios);

}

int kbhit() // use select to check for a keyboard hit
{
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}

int getch() // Pick up the character that is on the keyboard
{
    int r;
    unsigned char c;
    if ((r = read(0, &c, sizeof(c))) < 0) {
        return r;
    } else {
        return c;
    }
}
//--- END OF KEYBOARD SCANNING FUNCTIONS







int main(int argc, char *argv[]){
   int count;
   char command[100];
   char command1[100];
   char myorder[100];
   unsigned char receive[100];

   char arduino_string[100];


unsigned int a;
	printf("about to setup the terminal\n");
    //   set_conio_terminal_mode();
    printf("about to setup the interfaces\n");
    a = setup_interfaces();
    printf("about to reset the motors\n");
    reset_interfaces();
    printf("reset the motors\n");

    /*
     * Set up the input queue from comms_server to process messages from Netio
     */
    int msqid;
     int return_status;
     key_t key;
     struct msgbuf rcvbuffer; /* the receive buffer */

     key = 1234; // This is the key for the queue from the comms server

     if ((msqid = msgget(key, 0666)) < 0)
       die("msgget()");

     /*
      * Set up the output queue to comms_server so we can send collision messages to Netio user interface
      *
      */

     int msqido;
     int msgflg = IPC_CREAT | 0666;
     key_t okey;
     struct msgbuf sbuf; /* the send buffer */
     size_t obuflen;

     okey = 4321; /* Unique key for the send queue from the robogo program */

     if ((msqido = msgget(okey, msgflg )) < 0)   //Get the message queue ID for the given key
       die("msgget");



     //Message Type
     sbuf.mtype = 1;



while (1==1) {
    //Receive an answer of message type 1.
    if ((msgrcv(msqid, &rcvbuffer, 128, 1, IPC_NOWAIT)) < 0){
      if (errno == ENOMSG) {
      //check for collisions
    	  read_arduino();
    	 // printf("%d %d %difconfig \n",l_distance,c_distance,r_distance);



          // if we are closer than 10cm from an onstacle and we are not already avoiding a collision then
          // jump to the avoid_collision() 

    	  if (((l_distance<10)||(c_distance<10)||(r_distance<10)) && !in_collision) {
    		  avoid_collision();
    		  in_collision= TRUE;
    	  }

    	  if (!((l_distance<10)||(c_distance<10)||(r_distance<10)))  in_collision=FALSE;


      }
      else
      die("msgrcv");
    }
    else
    {
        // now that we have a message do something with it

    strcpy(command,rcvbuffer.mtext);
    if (DEBUG_ON == TRUE) printf("\n\rCommand is %s\n\r", rcvbuffer.mtext);

    if (strcmp(command,"forward")==0){
    	printf("forward\n\r");
    	drive_motors("F2000\r","F2000\r",0);
    	// set the motors to forward etc
    }
    if (strcmp(command,"left")==0) {
    	printf("left\n\r");
    	drive_motors("F1600\r","F2000\r",0);
    }
    if (strcmp(command,"back")==0){
    	printf("back\n\r");
    	drive_motors("R2000\r","R2000\r",0);

    }
    if (strcmp(command,"right")==0){
    	printf("right\n\r");
    	drive_motors("F2000\r","F1600\r",0);
    }
    if (strcmp(command,"360")==0){
    	printf("right\n\r");
    	drive_motors("F2000\r","R2000\r",3);
    }
    if (strcmp(command,"stop")==0){

        printf("stop\n\r");
	    drive_motors("X\r","X\r",0);
    }
    if (strcmp(command,"exit")==0) {
        printf("Exit\r\n");
        break; 
    }
   // printf("[%d] [%d] [%d] \n", a,a,a );

}

}
          printf("Exiting Program\n");
          reset_terminal_mode();
          close(l_motor_fd);
          close(r_motor_fd);
          close(u_sensor_fd);
          return 0;
}



