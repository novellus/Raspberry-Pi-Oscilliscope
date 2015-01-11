#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdlib.h>

volatile unsigned int* gpio; //Memory map to GPIO access.
int scrn_fd; //File descriptor for the framebuffer.

#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) INP_GPIO(g);*(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
//Here OUT calls INP to zero the first two bits of the GPIO-pin control, before setting the last bit.

volatile int nopTmp;
#define nop() nopTmp+1;
//This nop is about 10.7ns

//read macro; Data is read into SPI_data
unsigned int SPI_data=0;
int SPI_i;
#define SPI_read12() \
SPI_data=0; \
(*CLR)=SCLK; \
(*SET)=CS; \
for(SPI_i=0;SPI_i<36;SPI_i++) nop(); /*Delay for 1.4us*/ \
(*CLR)=CS; \
for(SPI_i=0;SPI_i<105;SPI_i++) nop(); /*Delay for 3.7us*/ \
for(SPI_i=0;SPI_i<12;SPI_i++) { \
	(*SET)=SCLK; \
	SPI_data+=(((*READ)&MISO)?1<<(11-SPI_i):0); \
	(*CLR)=SCLK; \
	nop(); nop(); nop(); nop(); /*Meeting the 8MHz max clock frequency timing requirement of the ADC*/ \
}

//Defining a string of 3360 bytes for the compiler to deal with. //They are all Q's, maybe I'll come up with a better name later.
#define Q0 'd'
#define Q Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0,Q0
#define Q2 Q,Q,Q,Q,Q,Q,Q,Q,Q,Q
#define Q3 Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2,Q2
#define FILL3360GREEN Q3,Q3,Q3,Q2,Q2,Q2,Q,Q,Q,Q,Q,Q

unsigned char green[3360]={FILL3360GREEN}; //One line of green.
unsigned char black[3360]={0}; //One line of black.
//Screen Size is 1680x1056; two bytes for each pixel.

int setup_io();
void killHandler(int);

int main()
{
	scrn_fd=open("/dev/fb0", O_RDWR|O_SYNC); //FrameBuffer
	if(!setup_io() || scrn_fd==-1) {
		printf("Init Failed\r\n");
		return 0;
	}
	else printf("Init Successful\r\n");
	signal(SIGINT,killHandler); //Exiting Gracefully

	volatile unsigned int* SET=gpio+7;
	volatile unsigned int* CLR=gpio+10;
	volatile unsigned int* READ=gpio+13;
	//Setting or clearing pins takes about 28.8ns; Reading takes about ?ns
	//This puts max signalling speed around 17Mhz; gcc option -O3 brings it over 20Mhz.

	OUT_GPIO(11); //SCLK
	INP_GPIO(9); //MISO
	OUT_GPIO(8); //Chip Select

	//Predefining these for performance.
	unsigned int SCLK=1<<11;
	unsigned int MISO=1<<9;
	unsigned int CS=1<<8;

	//Blacken the screen.
	for(int i=0;i<1056;i++) {
		lseek(scrn_fd,i*3360,SEEK_SET);
		write(scrn_fd,black,3360);
	}

	//Turns the screen into a 1000 line horizontal bar graph of voltage, cycling through from bottom to top again.
	//This currently runs at only ~54kHz. gcc option -O3 brings it over 100khz, but that flag also kills my nop() routine...
	unsigned int tmp;
	int i=0;
	while(1) {
		SPI_read12();
		lseek(scrn_fd,i*3360,SEEK_SET);
		tmp=SPI_data/3+1680;
		write(scrn_fd,green,tmp);
		write(scrn_fd,black,3360-tmp);
		i++;i=i%1000;
	}

	return 0;
}

//Maps the GPIO Access point to memory.
int setup_io()
{
	int mem_fd = open("/dev/mem", O_RDWR|O_SYNC); //Physical memory, including GPIO Access.
	if(mem_fd<0) return 0;
	void* gpio_map = mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,mem_fd,0x20200000); //0x20200000 is the offset to the GPIO peripheral map portion of memory.
	close(mem_fd);
	if (gpio_map == MAP_FAILED) return 0;
	gpio = (volatile unsigned int*)gpio_map;
	return 1;
}

//Gracefully blackens the screen before killing the program to avoid GUI bleedthroughs...
void killHandler(int sig) {
	for(int i=0;i<1056;i++) {
		lseek(scrn_fd,i*3360,SEEK_SET);
		write(scrn_fd,black,3360);
	}
	close(scrn_fd);
	exit(0);
}
