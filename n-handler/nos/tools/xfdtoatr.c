/** 
 * Quick tool to turn output XFD to ATR
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISK_SIZE 92176

FILE *xfdfp;
FILE *atrfp;
int len=0;
int len2=0;
int paddingSize=0;

const unsigned char atrheader[16]=
  {0x96, 0x02, 0x80, 0x16, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

unsigned char buffer[256];
unsigned char padding_buffer[131072];

int main(int argc, char* argv[])
{
  if (argc!=3)
    {
      printf("%s <xfd> <atr>\n\n",argv[0]);
      return 1;
    }

  xfdfp = fopen(argv[1],"r+");

  if (!xfdfp)
    {
      printf("Could not open XFD file %s, aborting.\n\n",argv[1]);
      return 1;
    }

  atrfp = fopen(argv[2],"w+");

  if (!atrfp)
    {
      printf("Could not open ATR file %s, aborting.\n\n",argv[2]);
      return 1;
    }

  // Write ATR header
  len = fwrite(&atrheader, sizeof(unsigned char), sizeof(atrheader), atrfp);

  if (len!=16)
    {
      printf("Could not write ATR header to file %s, aborting. \n\n",argv[2]);
      return 1;
    }

  len2 += 16;
  
  // Write XFD data to ATR.
  while (!feof(xfdfp))
    {
      len = fread(&buffer, sizeof(unsigned char), sizeof(buffer), xfdfp);
      fwrite(&buffer, sizeof(unsigned char), sizeof(buffer), atrfp);
      len2 += len;
    }

  // Now calculate and write padding data.
  paddingSize = (DISK_SIZE - len2)-28; // WHY?!
  fwrite(&padding_buffer, 1, paddingSize, atrfp);

  // And, we're done.
  fclose(xfdfp);
  fclose(atrfp);
  return 0;
}
