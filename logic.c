#include "logic.h"

void alarm_function(){
	printf("Alarm #%d\n", counter);
	flag=1;
	counter++;
}

int setup(char *port) {
  fd = open(port, O_RDWR | O_NOCTTY );
  if (fd <0) {perror(port); return ERROR; }

  if ( tcgetattr(fd,&oldtio) == ERROR) { /* save current port settings */
    perror("tcgetattr");
    return ERROR;
  }

  bzero(&newtio, sizeof(newtio));
  newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
  newtio.c_iflag = IGNPAR;
  newtio.c_oflag = 0;

  /* set input mode (non-canonical, no echo,...) */
  newtio.c_lflag = 0;

  newtio.c_cc[VTIME]    = 1;   /* inter-character timer unused */
  newtio.c_cc[VMIN]     = 0;   /* blocking read until 5 chars received */

  tcflush(fd, TCIOFLUSH);

  if ( tcsetattr(fd,TCSANOW,&newtio) == ERROR) {
    perror("tcsetattr");
    return ERROR;
  }

  printf("New termios structure set\n");
  return fd;
}

int llopen_Receiver(){
  int i = 0;
  int state = 0;
  int isValidSet;

  char ua[5] = {F , UA_A , UA_C , UA_BCC1 , F};

  while(STOP==FALSE) {
    res = read(fd, buf+i, 1);

    switch(state) {
      case 0:  
        if(res > 0 && *(buf+i) == F) {
          i++;
          state++;
        }
        break;
      case 1:
        if(res > 0 && *(buf+i) != F) {
          i++;
          state++;
        } 
        break;
      case 2:
        if(res > 0 && *(buf+i) != F) {
          i++;
        }
        else if (res > 0 && *(buf+i) == F) {
          state++;
        }                                          
        break;
      default:
        isValidSet = (buf[3] == (XOR(buf[1], buf[2])));
        printf("Valid SET ? %s\n", isValidSet ? "true" : "false");
        if(isValidSet){
          STOP = TRUE;
          res = write(fd,ua,sizeof(char)*5);
          printf("%d bytes written\n", res);
        }
        else{
          i = 0;
          state = 0;
        }
        break;
    }
  }

  //printf("Received: %s\n", buf);

    
  //writing back to the emissor
  //int size = strlen(buf) + 1;

  //res = write(fd,buf,size);

  fflush(NULL);
  //printf("Sending back...\n");
  //printf("%d bytes written\n", res);

  sleep(1);
  tcsetattr(fd,TCSANOW,&oldtio);
  
  close(fd);
  return 0;
}

int llopen_Sender(){
  (void) signal(SIGALRM, alarm_function);
  char set[5] = {F , 0x03 , 0x03 , 0x00 , F};

  int i;

  while (STOP==FALSE && counter < 3) {
    i = 0;

    res = write(fd,set,sizeof(char) * 5);
    printf("%d bytes written\n", res);
		fflush(NULL);

		alarm(3);
		flag = 0;

		while(flag == 0 && STOP==FALSE){
			res = read(fd,buf+i,1);
			if (i==0 && buf[0] == F){
				i++;
			}
			else if(i > 0 && buf[i]!=F){
				i++;
			}
			else if(i > 0 && buf[i]==F) {
				if (i > 3  && buf[3] == (XOR(buf[1], buf[2])))
					STOP = TRUE;
			}
		}
  }
		
  if(STOP == TRUE){
		printf("Received: 0x%x\n", buf[2]);
	}
	else
		printf("Connection not established after 3 attempts\n");

  sleep(1);
  if ( tcsetattr(fd,TCSANOW,&oldtio) == ERROR) {
    perror("tcsetattr");
    return ERROR;
  }

  close(fd);
  return 0;
}

int llopen(int type){
  if(type == TRANSMITTER)
  {
    return llopen_Sender();
  }
  else if (type == RECEIVER)
  {
    return llopen_Receiver();
  }
  
  return ERROR;
}
