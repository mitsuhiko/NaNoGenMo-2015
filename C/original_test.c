/************************************************************************
*
* Copyright 2015 by Sean Conner.  All Rights Reserved.
* Modified for macOS testing (vm86 removed)
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
*************************************************************************/

/* Quick test version to verify loading works */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define SEG_ENV		0x1000
#define SEG_PSP		0x2000
#define SEG_LOAD	0x2010

#define MEM_ENV		(SEG_ENV  * 16)
#define MEM_PSP		(SEG_PSP  * 16)
#define MEM_LOAD	(SEG_LOAD * 16)

typedef struct exehdr
{
  uint8_t  magic[2];	/* 0x4D, 0x5A */
  uint16_t lastpagesize;
  uint16_t filepages;
  uint16_t numreloc;
  uint16_t hdrpara;
  uint16_t minalloc;
  uint16_t maxalloc;
  uint16_t init_ss;
  uint16_t init_sp;
  uint16_t chksum;
  uint16_t init_ip;
  uint16_t init_cs;
  uint16_t reltable;
  uint16_t overlay;
} __attribute__((packed)) exehdr__s;

static void dump_exehdr__s(const exehdr__s *hdr)
{
  fprintf(
    stderr,
    "EXE Header:\n"
    "lastpage:  %d\n"
    "filepages: %d (%u bytes)\n"
    "numreloc:  %d\n"
    "hdrpara:   %d\n"
    "minalloc:  %d\n"
    "maxalloc:  %d\n"
    "SS:SP:     %04X:%04X\n"
    "CS:IP:     %04X:%04X\n"
    "reltable:  %04X\n"
    "overlay:   %d\n"
    "\n",
    hdr->lastpagesize,
    hdr->filepages, hdr->filepages * 512 + hdr->lastpagesize,
    hdr->numreloc,
    hdr->hdrpara,
    hdr->minalloc,
    hdr->maxalloc,
    hdr->init_ss, hdr->init_sp,
    hdr->init_cs, hdr->init_ip,
    hdr->reltable,
    hdr->overlay
  );
}

int main(int argc, char *argv[])
{
  FILE *fp;
  exehdr__s hdr;
  struct stat st;
  
  if (argc < 2)
  {
    fprintf(stderr, "usage: %s file\n", argv[0]);
    exit(2);
  }

  fp = fopen(argv[1], "rb");
  if (fp == NULL)
  {
    perror(argv[1]);
    exit(1);
  }
  
  fstat(fileno(fp), &st);
  printf("File size: %lld bytes\n", (long long)st.st_size);
  
  /* Read first few bytes to see if it's an EXE */
  fread(&hdr, sizeof(hdr), 1, fp);
  
  if ((hdr.magic[0] == 0x4D) && (hdr.magic[1] == 0x5A))
  {
    printf("This is an EXE file!\n");
    dump_exehdr__s(&hdr);
    
    /* Try to run the first instruction and see what happens */
    unsigned char *mem = malloc(1024 * 1024);
    if (mem)
    {
      memset(mem, 0, 1024 * 1024);
      
      size_t offset = hdr.hdrpara * 16;
      size_t binsize = (hdr.filepages * 512 + hdr.lastpagesize) - offset;
      
      fseek(fp, offset, SEEK_SET);
      fread(&mem[MEM_LOAD], 1, binsize, fp);
      
      printf("Loaded %zu bytes at offset %04X\n", binsize, MEM_LOAD);
      printf("Entry point: %04X:%04X\n", hdr.init_cs + SEG_LOAD, hdr.init_ip);
      
      /* Show first few bytes at entry point */
      size_t entry = (hdr.init_cs + SEG_LOAD) * 16 + hdr.init_ip;
      printf("First instructions at entry: ");
      for (int i = 0; i < 16; i++)
      {
        printf("%02X ", mem[entry + i]);
      }
      printf("\n");
      
      free(mem);
    }
  }
  else
  {
    printf("This appears to be a COM file (not EXE)\n");
    printf("First few bytes: ");
    rewind(fp);
    for (int i = 0; i < 16; i++)
    {
      int c = fgetc(fp);
      if (c == EOF) break;
      printf("%02X ", c);
    }
    printf("\n");
  }
  
  fclose(fp);
  return 0;
}