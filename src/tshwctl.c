#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <linux/types.h>
#include <math.h>

#include "fpga.h"

static twifd = -1;

int fpgadio_read(int dio) {
	uint8_t data = fpeek8(twifd, dio);
	if(data & 0x4) return 1;
	else return 0;
}

int fpgadio_set(int dio, int value) {
	if(value) fpoke8(twifd, dio, 0x3);
	else fpoke8(twifd, dio, 0x1);
}

// Value 1 is output, 0 is input
int fpgadio_ddr(int dio, int value) {
	if(value) fpoke8(twifd, dio, 0x1);
	else fpoke8(twifd, dio, 0x0);
}

// Calculate the number of 24mhz clocks for a given
// baud rate / bits per symbol
// For example:
////// CNT 1
//// 115200 is 8681ns byte time
//// 8681*9.5=82469.5ns
//// 24mhz is 41.67ns
//// 82469.5/41.67 = 1979 (CNT1)
////// CNT 2
//// 4340.5ns is .5 bit time
//// 4340.5/41.67 = 104 (CNT2)
void autotx_bitstoclks(int bits, int baud, uint32_t *cnt1, uint32_t *cnt2)
{
	float clk;
	const float fpga_clk = 41.67; // ns

	// get baud byte time in ns
	clk = (bits*100000000)/baud;
	*cnt1 = lround((clk*9.5) / fpga_clk);
	*cnt2 = lround((clk*0.5) / fpga_clk);
}

void usage(char **argv) {
	fprintf(stderr,
		"Usage: %s [OPTIONS] ...\n"
		"Technologic Systems TS-4900 Utility\n"
		"\n"
		"  -p, --getin <dio>      Returns the input value of an FPGA DIO\n"
		"  -e, --setout <dio>     Sets an FPGA DIO output value high\n"
		"  -l, --clrout <dio>     Sets an FPGA DIO output value low\n"
		"  -d, --ddrout <dio>     Set FPGA DIO to an output\n"
		"  -r, --ddrin <dio>      Set FPGA DIO to an input\n"
		"  -m, --addr <address>   Sets up the address for a peek/poke\n"
		"  -v, --poke <value>     Writes the value to the specified address\n"
		"  -t, --peek             Reads from the specified address\n"
		"  -b, --baud <baud>      Specifies the baud rate for auto485\n"
		"  -x, --bits <bits>      Specifies the bit size for auto485 (8n1 = 10)\n"
		"  -h, --help             This message\n"
		"\n",
		argv[0]
	);
}

int main(int argc, char **argv) 
{
	int c;
	uint16_t addr = 0x0, val;
	int opt_addr = 0;
	int baud = 0, bits = 0;

	static struct option long_options[] = {
		{ "getin", 1, 0, 'p' },
		{ "setout", 1, 0, 'e' },
		{ "clrout", 1, 0, 'l' },
		{ "ddrout", 1, 0, 'd' },
		{ "ddrin", 1, 0, 'r' },
		{ "addr", 1, 0, 'm' },
		{ "poke", 1, 0, 'v' },
		{ "peek", 0, 0, 't' },
		{ "baud", 1, 0, 'b' },
		{ "bits", 1, 0, 'x' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	twifd = fpga_init();
	if(twifd == -1) {
		perror("Can't open FPGA I2C bus");
		return 1;
	}

	while((c = getopt_long(argc, argv, "+p:e:l:d:r:m:v:tb:x:h", long_options, NULL)) != -1) {
		int gpiofd;
		int gpio, i;
		uint32_t cnt1, cnt2;

		switch(c) {
		case 'p':
			gpio = atoi(optarg);
			printf("gpio%d=%d\n", gpio, fpgadio_read(gpio));
			break;
		case 'e':
			gpio = atoi(optarg);
			fpgadio_set(gpio, 1);
			break;
		case 'l':
			gpio = atoi(optarg);
			fpgadio_set(gpio, 0);
			break;
		case 'd':
			gpio = atoi(optarg);
			fpgadio_ddr(gpio, 1);
			break;
		case 'r':
			gpio = atoi(optarg);
			fpgadio_ddr(gpio, 0);
			break;
		case 'm':
			opt_addr = 1;
			addr = strtoull(optarg, NULL, 0);
			break;
		case 'v':
			if(opt_addr) {
				val = strtoull(optarg, NULL, 0);
				fpoke8(twifd, addr, val);
			} else {
				fprintf(stderr, "No address specified\n");
				break;
			}
			break;
		case 't':
			if(opt_addr) {
				printf("addr%d=0x%X\n", addr, fpeek8(twifd, addr));
			} else {
				fprintf(stderr, "No address specified\n");
				break;
			}
			break;
		case 's':
			i = fpeek8(twifd, 31);
			printf("pushsw=%d\n", i ? 0 : 1);
			break;
		case 'b':
			baud = atoi(optarg);
			break;
		case 'x':
			bits = atoi(optarg);
			break;
		case 'q':
			if(baud == 0 || bits == 0) {
				fprintf(stderr, "You must set baud and base to a non-zero value\n");
				return 1;
			}

			if(atoi(optarg) == 1) {
				autotx_bitstoclks(bits, baud, &cnt1, &cnt2);
				fpoke8(twifd, 32, (uint8_t)((cnt1 & 0xff0000) >> 16));
				fpoke8(twifd, 33, (uint8_t)((cnt1 & 0xff00) >> 8));
				fpoke8(twifd, 34, (uint8_t)(cnt1 & 0xff));
				fpoke8(twifd, 35, (uint8_t)((cnt2 & 0xff0000) >> 16));
				fpoke8(twifd, 36, (uint8_t)((cnt2 & 0xff00) >> 8));
				fpoke8(twifd, 37, (uint8_t)(cnt2 & 0xff));
			} 
			break;
		case 'w':
			if(baud == 0 || bits == 0) {
				fprintf(stderr, "You must set baud and base to a non-zero value\n");
				return 1;
			}
			if(atoi(optarg) == 1) {
				autotx_bitstoclks(bits, baud, &cnt1, &cnt2);
				fpoke8(twifd, 38, (uint8_t)((cnt1 & 0xff0000) >> 16));
				fpoke8(twifd, 39, (uint8_t)((cnt1 & 0xff00) >> 8));
				fpoke8(twifd, 40, (uint8_t)(cnt1 & 0xff));
				fpoke8(twifd, 41, (uint8_t)((cnt2 & 0xff0000) >> 16));
				fpoke8(twifd, 42, (uint8_t)((cnt2 & 0xff00) >> 8));
				fpoke8(twifd, 43, (uint8_t)(cnt2 & 0xff));
			}
			break;
		default:
			usage(argv);
		}
	}

	close(twifd);

	return 0;
}

