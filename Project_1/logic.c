// Logic -- Data Link Layer

#include "logic.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <sys/time.h>

struct termios oldtio, newtio;
LinkLayer linkLayer;
Statistics statistics;
struct timeval timerStart, timerEnd;

int flag = 0;
int counter = 0;
int fd;
char C_FLAG = 0x0;
int program;

int checkFrame(Frame f);
int sendMsg(Frame f);
int llclose_Receiver();
int llclose_Sender();
void connectionInfo();

char getCFlag(){
	if (C_FLAG == 0x00){
		C_FLAG = 0x40;
		return 0x0;
	} else {
		C_FLAG = 0x00;
		return 0x40;
	}
}

void alarm_function(){
	statistics.time_outs++;
	flag=1;
	counter++;
}

void createStructLinkLayer(char *port){
	linkLayer.baudRate = BAUDRATE;
	linkLayer.numTransmissions = NUMBER_OF_TRIES;
	linkLayer.port = port;
	linkLayer.timeout = 3;
}

void initializeStatistics(){
	statistics.sent = 0;
	statistics.received = 0;
	statistics.time_outs = 0;
	statistics.sentRR = 0;
	statistics.receivedRR = 0;
	statistics.sentREJ = 0;
	statistics.receivedREJ = 0;
	statistics.framesTotalTime = 0;
	statistics.framesCounter = 0;
	statistics.errorProbability_data = (float)ERROR_PROBABILITY_DATA;
	statistics.errorProbability_header = (float)ERROR_PROBABILITY_HEADER;
}

int setup(char *port) {
  createStructLinkLayer(port);
  initializeStatistics();
  fd = open(linkLayer.port, O_RDWR | O_NOCTTY );
  if (fd <0) {perror(linkLayer.port); return ERROR; }

  if ( tcgetattr(fd,&oldtio) == ERROR) { /* save current port settings */
    perror("tcgetattr");
    return ERROR;
  }

  bzero(&newtio, sizeof(newtio));
  newtio.c_cflag = linkLayer.baudRate | CS8 | CLOCAL | CREAD;
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

  printf("\n*** New termios structure set ***\n");
  return fd;
}

int readFrame(Frame* f){

	char* buf = malloc(sizeof(char) * 10);
	int bufferSize = 10;
	int res, i = 0;
	int state = 0;

	while(flag == 0){
		if(i == bufferSize) {
			bufferSize *= 2;
			buf = realloc(buf, sizeof(char) * bufferSize);
		}

		res = read(fd,buf+i,1);

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
				i++;
			}
			break;
		default:
			f->msg = buf;
			f->length = i;
			return 0;
		}
	}

	return ERROR;
}

int rejectFrame(char cflag){
	char c;
	if (cflag == I1_C){
		c = REJ1_C;
	}
	else if (cflag == I0_C){
		 c = REJ0_C;
	}
	else{
		return ERROR;
	}

	char* temp = malloc(sizeof(char) * 5);
	temp[0] = F; temp[1] = A1; temp[2] = c; temp[3] = XOR(A1, c); temp[4] = F;

	Frame rej = {
		.msg = temp,
		.length = 5
	};

	// write reject
	int ret = sendMsg(rej);
	free(rej.msg);

	statistics.sentREJ++;

	return ret;
}

int acceptFrame(char cflag){
	char c;
	if (cflag == I1_C){
		c = RR0_C;
	}
	else if (cflag == I0_C){
		 c = RR1_C;
	}
	else{
		return ERROR;
	}

	char* temp = malloc(sizeof(char) * 5);
	temp[0] = F; temp[1] = A1; temp[2] = c; temp[3] = XOR(A1, c); temp[4] = F;

	Frame rr = {
		.msg = temp,
		.length = 5
	};

	// write accept
	int ret = sendMsg(rr);
	free(rr.msg);

	statistics.sentRR++;

	return ret;
}

char* destuffing(char* buf, int* size) {
	int length = *size;
	int new_size = 0;
	int i, j;

	for(i = 0; i < length; i++) {
		if (buf[i] == 0x7d) {
			i++;
		}
		new_size++;
	}

	char* ret = malloc(sizeof(char) * new_size);

	for (i = 0, j = 0; i < length; i++, j++) {
		if(buf[i] == 0x7d) {
			if(buf[i+1] == 0x5e) {
				ret[j] = 0x7e;
				i++;
			}
			else if(buf[i+1] == 0x5d) {
				ret[j] = 0x7d;
				i++;
			}
			else
				return NULL;
		}
		else {
			ret[j] = buf[i];
		}
	}

	*size = new_size;
	return ret;
}

char* deconstructFrame(Frame f, int* size) {
	int new_size = f.length - 5;
	char* ret = malloc(sizeof(char) * new_size);
	ret = memcpy(ret, f.msg + 4, new_size);

	*size = new_size;
	return ret;
}

int checkBCC2(char* buf, int size) {
	int i, bcc2 = buf[0];

	for(i = 1; i < size - 1; i++) {
		bcc2 = XOR(bcc2, buf[i]);
	}

	if(bcc2 == buf[size - 1]) {
		return TRUE;
	}
	else {
		return FALSE;
	}
}

void simulateErrors(Frame* f) {
	size_t length = f->length;

	srand((unsigned int) time(NULL));

	float r = rand() / ((float) RAND_MAX);
	r *= 100;

	//Insert error in data
	if(r < statistics.errorProbability_data) {
		int index = rand() % (length - 6);
		index += 4;
		f->msg[index] = rand();
	}

	//Insert error in frame headers
	r = rand() / ((float) RAND_MAX);
	r *= 100;
	if(r < statistics.errorProbability_header) {
		int index = rand() % 4;
		index += 1;
		if(index == 4) index = length - 2;
		f->msg[index] = rand();
	}
}

int llread(char **buffer){

	Frame f;

	int ret = readFrame(&f);

	if(ret == ERROR)
		return ERROR;

	if((f.msg[2] == I0_C || f.msg[2] == I1_C) && f.msg[4] == 0x01) {
		simulateErrors(&f);
	}

	int frame_type = checkFrame(f);

	if(frame_type == ERROR) {
		return 0;
	}

	if(frame_type == DISC && f.msg[1] == A1) {
		return -2;
	}

	statistics.received++;
	if(frame_type != I0 && frame_type != I1) {
		rejectFrame(C_FLAG);
		return 0;
	}
	if (f.msg[2] != C_FLAG) {
		rejectFrame(C_FLAG);
		return 0;
	}

	int size;

	// remover cabeçalho e flag inicial
	char* buf1 = deconstructFrame(f, &size);
	free(f.msg);

	// extrair pacote de comando da trama - destuffing
	char* buf2 = destuffing(buf1, &size);
	free(buf1);

	if(buf2 == NULL) {
		rejectFrame(C_FLAG);
		return 0;
	}

	// ver valor do bcc2 se está correcto
	if(checkBCC2(buf2, size)) {
		size--;
		buf2 = realloc(buf2, sizeof(char) * size);
	}
	else {
		rejectFrame(C_FLAG);
		return 0;
	}

	//check data L1 and L2 with actual size
	if(buf2[0] == 0x01) {
		size_t data_size = 256 * (unsigned char) buf2[2] + (unsigned char) buf2[3];
		if(data_size != size - 4) {
			rejectFrame(C_FLAG);
			return 0;
		}
	}

	*buffer = buf2;
	acceptFrame(C_FLAG);

	getCFlag();

	return size;
}

int llopen_Receiver(){

	char* temp = malloc(sizeof(char) * 5);
	temp[0] = F; temp[1] = A1; temp[2] = UA_C; temp[3] = UA_BCC1; temp[4] = F;

	Frame ua = {
		.msg = temp,
		.length = 5
	};

	Frame f;
	int ret;

	while(1){
		ret = readFrame(&f);

		if (ret != ERROR) {
			int frame_type = checkFrame(f);
			if(frame_type == SET  && f.msg[1] == A1) {
				sendMsg(ua);
				free(f.msg);
				free(ua.msg);
				return 0;
			}
		}
	 }

	return ERROR;
}

int sendMsg(Frame f) {

	// write on serial port
	int res = write(fd, f.msg, sizeof(char) * f.length);
	return res;
}

int sendFrame(Frame f, Frame* response){
	int STOP = FALSE;

	// reset global counter
	counter = 0;

	while (STOP==FALSE && counter < linkLayer.numTransmissions) {
		sendMsg(f);
		alarm(linkLayer.timeout);
		flag = 0;
		if (readFrame(response) != ERROR){
			alarm(0);
			STOP = TRUE;
		}
  }

  if(STOP != TRUE)
	return ERROR;

  return 0;
}

int checkFrame(Frame f) {
	if(f.msg[3] != (XOR(f.msg[1], f.msg[2]))) {
		return ERROR;
	}

	switch(f.msg[2]) {
		case (char)SET_C:
			return SET;
		case (char)DISC_C:
			return DISC;
		case (char)UA_C:
			return UA;
		case (char)RR0_C:
			return RR0;
		case (char)RR1_C:
			return RR1;
		case (char)REJ0_C:
			return REJ0;
		case (char)REJ1_C:
			return REJ1;
		case (char)I0_C:
			return I0;
		case (char)I1_C:
			return I1;
		default:
			return ERROR;
	}
}

int llopen_Sender(){
	char* temp = malloc(sizeof(char) * 5);
	temp[0] = F; temp[1] = A1; temp[2] = SET_C; temp[3] = SET_BCC1; temp[4] = F;

	Frame set = {
		.msg = temp,
		.length = 5
	};

	Frame response;

	int ret = sendFrame(set, &response);

	free(set.msg);

	if (ret != ERROR) {
		int frame_type = checkFrame(response);
		if(frame_type == UA  && response.msg[1] == A1) {
			free(response.msg);
			return 0;
		}
	}

	return ERROR;
}

int llopen(int type){
  program = type;

  connectionInfo();

  if(program == TRANSMITTER){
    return llopen_Sender();
  }
  else if (program == RECEIVER){
    return llopen_Receiver();
  }

  return ERROR;
}

char *generateBCC2(char *buffer, int *size){
	int length = *size;
	char *buff1 = malloc(sizeof(char) * (length + 1));

	int i;
	int bcc2 = buffer[0];
	for(i = 1; i < length; i++) {
		bcc2 = XOR(buffer[i], bcc2);

	}

	memcpy(buff1, buffer, length);

	// add bcc2 to buff1
	buff1[length] = bcc2;

	*size = length + 1;

	return buff1;
}

char *stuffing(char *buffer, int *finalSize){
	int length = *finalSize;
	int i, j, new_size = 0;

	// count number of 0x7e and 0x7d
	for(i = 0; i < length; i++){
		if(buffer[i] == 0x7e || buffer[i] == 0x7d){
			new_size++;
		}
		new_size++;
	}

	char *buff = malloc(sizeof(char) * new_size);

	for(i = 0, j= 0; i < length; i++){
		if(buffer[i] == 0x7e){
			buff[j] = 0x7d;
			buff[j+1] = 0x5e;
			j++;
		}
		else if (buffer[i] == 0x7d){
			buff[j] = 0x7d;
			buff[j+1] = 0x5d;
			j++;
		} else {
			buff[j] = buffer[i];
		}
		j++;
	}

	*finalSize = new_size;
	return buff;
}

Frame createFrame(char *buffer, int size){
	int length = size;
	char *msg = malloc(length + 5);

	msg[0] = F;
	msg[1] = A1;
	msg[2] = getCFlag();

	// add bcc1
	msg[3] = XOR(msg[1], msg[2]);

	// copy buffer
	memcpy(msg+4, buffer, length);

	msg[length + 4] = F;

	Frame frame = {
		.msg = msg,
		.length = length + 5
	};

	return frame;
}

int llwrite(char *buffer, int length){
	int finalSize = length;

	// calculate BCC2
	char *buff1 = generateBCC2(buffer, &finalSize);

	//stuffing
	char *buff2 = stuffing(buff1, &finalSize);
	free(buff1);

	// junta cabecalho e flag final
	Frame f = createFrame(buff2, finalSize);
	free(buff2);

	Frame response;

	int frame_type_send = checkFrame(f);
	int rej = 1;

	gettimeofday(&timerStart, NULL);

	do {
		int ret = sendFrame(f, &response);
		if(ret != ERROR) {
			statistics.sent++;
			int frame_type_response = checkFrame(response);

			if((frame_type_response == RR0) || (frame_type_response == RR1))
				statistics.receivedRR++;
			if((frame_type_response == REJ0) || (frame_type_response == REJ1))
				statistics.receivedREJ++;

			if(frame_type_response != ERROR){
				if ((frame_type_send == I1 && frame_type_response == RR0) ||
					(frame_type_send == I0 && frame_type_response == RR1)) {
					rej = 0;
				}
				else if ((frame_type_send == I1 && frame_type_response == REJ1) ||
					(frame_type_send == I0 && frame_type_response == REJ0)) {
					rej = 1;
				}
				else {
					rej = 0;
				}
			}
			else{
				rej = 1;
			}
		}
		else {
			return ERROR;
		}
	} while(rej);

	gettimeofday(&timerEnd, NULL);
	statistics.framesTotalTime += (timerEnd.tv_sec - timerStart.tv_sec)*1000.0f +
																(timerEnd.tv_usec - timerStart.tv_usec)/1000.0f;
	statistics.framesCounter++;

	free(f.msg);
	return 0;
}

int llclose(){
  int ret;

  if(program == TRANSMITTER){
    ret = llclose_Sender();
  }
  else if (program == RECEIVER){
    ret = llclose_Receiver();
  }

  if ( tcsetattr(fd,TCSANOW,&oldtio) == ERROR) {
    perror("tcsetattr");
    return ERROR;
  }

  if(close(fd) < 0 || ret < 0) {
    return ERROR;
  }
  return 0;
}

int llclose_Receiver() {
  char* temp = malloc(sizeof(char) * 5);
  temp[0] = F; temp[1] = A2; temp[2] = DISC_C; temp[3] = XOR(A2, DISC_C); temp[4] = F;

  Frame disc = {
    .msg = temp,
    .length = 5
  };

  Frame f;
  int ret;
	sendMsg(disc);
	free(disc.msg);

  ret = readFrame(&f);
  if(ret != ERROR) {
    int frame_type = checkFrame(f);
    if(frame_type == UA && f.msg[1] == A2) {
      free(f.msg);
      return 0;
    }
  }

  return ERROR;
}

int llclose_Sender() {
  char* temp = malloc(sizeof(char) * 5);
  temp[0] = F; temp[1] = A1; temp[2] = DISC_C; temp[3] = XOR(A1, DISC_C); temp[4] = F;

  Frame disc = {
    .msg = temp,
    .length = 5
  };

  Frame response;
  int ret = sendFrame(disc, &response);
  free(disc.msg);

  if(ret != ERROR) {
    int frame_type = checkFrame(response);
    if(frame_type == DISC && response.msg[1] == A2) {
      free(response.msg);

      //creating ua frame
      char* temp1 = malloc(sizeof(char) * 5);
      temp1[0] = F; temp1[1] = A2; temp1[2] = UA_C; temp1[3] = XOR(A2, UA_C); temp1[4] = F;

      Frame ua = {
        .msg = temp1,
        .length = 5
      };

      ret = sendMsg(ua);

      return 0;
    }
  }
  return ERROR;
}

void printProgressBar(int sizeReceived, int fileSize, size_t packageNumber){
	int j, n, m;
    m = sizeReceived*100/fileSize;
	printf("\r[");
    n = m*50/100;
    if(sizeReceived >= fileSize){
        m=100;
    }
    for(j = 0; j < 50; j++){
        if(n >= 0){
            printf("#");
            n--;
        }
        else{
            printf(" ");
        }
    }
    printf("] %d%% - Package number: %ld", m, packageNumber);
	if(m==100)
		printf("\n\n");
	fflush(stdout);
}

void connectionInfo(){
	char *mode;
	if(program == TRANSMITTER){
    	mode = "Transmitter";
	}
  	else{
    	mode = "Receiver";
  	}
	printf("\n");
	printf("**************** CONNECTION INFO ****************\n");
	printf(" - Mode: %s\n", mode);
	printf(" - Port: %s\n", linkLayer.port);
	printf(" - Baud rate: %d\n", linkLayer.baudRate);
	printf(" - Package Data Size: %d\n", PACKAGE_DATA_SIZE);
	printf(" - Number of tries: %d\n", linkLayer.numTransmissions);
	printf(" - Time-out interval: %d\n", linkLayer.timeout);
	printf("*************************************************\n");
	printf("\n");
}

void connectionStatistics(){
	printf("\n");
	printf("************* CONNECTION STATISTICS *************\n");
	printf(" - Messages Sent: %ld\n", statistics.sent);
	printf(" - Messages Received: %ld\n", statistics.received);
	printf(" - Time-outs: %ld\n", statistics.time_outs);
	printf(" - RR Sent: %ld\n", statistics.sentRR);
	printf(" - RR Received: %ld\n", statistics.receivedRR);
	printf(" - REJ Sent: %ld\n", statistics.sentREJ);
	printf(" - REJ Received: %ld\n", statistics.receivedREJ);
	if(program == TRANSMITTER)
		printf(" - Average Frame Time: %ld ms\n", statistics.framesTotalTime / statistics.framesCounter);
	if(program == RECEIVER) {
		printf(" - Error Probability (data): %.2f %% \n", statistics.errorProbability_data);
		printf(" - Error Probability (header): %.2f %% \n", statistics.errorProbability_header);
	}
	printf("*************************************************\n");
	printf("\n");
}
