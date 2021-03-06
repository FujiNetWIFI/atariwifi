/**
 * CIO Status call
 */

#include <atari.h>
#include <6502.h>
#include "sio.h"

extern unsigned char err;
extern unsigned char ret;
extern unsigned char aux1_save[8];
extern unsigned char aux2_save[8];

void _cio_status(void)
{

  err=siov(DEVIC_N,
	   OS.ziocb.drive,
	   's',
	   DSTATS_READ,
	   OS.dvstat,
	   4,
	   DTIMLO_DEFAULT,
	   OS.ziocb.aux1,
	   OS.ziocb.aux2);
  
  ret=OS.dvstat[0];
}
