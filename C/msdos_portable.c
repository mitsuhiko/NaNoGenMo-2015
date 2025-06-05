/************************************************************************
*
* Copyright 2015 by Sean Conner.  All Rights Reserved.
* Modified 2025 for portability to modern systems
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
* Comments, questions and criticisms can be sent to: sean@conman.org
*
*************************************************************************/

/* Portable version that doesn't require Linux vm86() */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>

#define SEG_ENV		0x1000
#define SEG_PSP		0x2000
#define SEG_LOAD	0x2010

#define MEM_ENV		(SEG_ENV  * 16)
#define MEM_PSP		(SEG_PSP  * 16)
#define MEM_LOAD	(SEG_LOAD * 16)

/********************************************************************/

typedef struct fcbs	/* short FCB block */
{
  char     drive;
  char     name[8];
  char     ext[3];
  uint16_t cblock;
  uint16_t recsize;
} __attribute__((packed)) fcbs__s;

typedef struct fcb
{
  char     drive;
  char     name[8];
  char     ext[3];
  uint16_t cblock;
  uint16_t recsize;
  uint32_t size;
  uint16_t date;
  uint16_t time;
  uint16_t rsvp0;
  uint8_t  crecnum;
  uint32_t relrec;
} fcb__s;

typedef struct psp
{
  uint8_t  warmboot[2];		/* 0xCD, 0x20 */
  uint16_t last_seg;
  uint8_t  rsvp0;
  uint8_t  oldmscall_jmp;	/* 0x9A, offlo, offhi, seglo, seghi */
  uint16_t oldmscall_off;
  uint16_t oldmscall_seg;
  uint16_t termaddr[2];
  uint16_t ctrlcaddr[2];
  uint16_t erroraddr[2];
  uint8_t  rsvp1[22];
  uint16_t envp;
  uint8_t  rsvp2[34];
  uint8_t  mscall[3];		/* 0xCD , 0x21 , 0xCB */
  uint8_t  rsvp3[9];
  fcbs__s  primary;
  fcbs__s  secondary;
  uint8_t  rsvp[4];
  uint8_t  cmdlen;
  uint8_t  cmd[127];
} __attribute__((packed)) psp__s;

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

/* Simple x86 registers structure */
typedef struct x86_regs {
  uint16_t ax, bx, cx, dx;
  uint16_t si, di, bp, sp;
  uint16_t cs, ds, es, ss;
  uint16_t ip;
  uint16_t flags;
} x86_regs;

typedef struct system
{
  x86_regs                regs;
  unsigned char          *mem;
  fcb__s                 *fcbs[16];
  FILE                   *fp[16];
  uint16_t                dtaseg;
  uint16_t                dtaoff;
  
  /*---------------------------------------------------------------------
  ; Basically, before we can actually return data, we need to wait for the
  ; prompt, which is
  ;
  ;	CR LF '>'
  ;
  ; When we see those three characters printed, then we can turn on input. 
  ; We keep going until we get a CR, then we turn output off.  This is
  ; totally a hack to get Racter working.  It is *NOT* a general purpose
  ; solution, but I don't care about a general purpose solution at this
  ; time.  This will work.
  ;---------------------------------------------------------------------*/
  
  bool input;
  char prompt[4];
  bool running;
} system__s;

static system__s g_sys;

/********************************************************************/

static void cleanup(void)
{
  if (g_sys.mem != NULL)
  {
    free(g_sys.mem);
    g_sys.mem = NULL;
  }
}

/********************************************************************/

static inline uint16_t get_word(unsigned char *mem, size_t offset)
{
  return mem[offset] | (mem[offset + 1] << 8);
}

static inline void set_word(unsigned char *mem, size_t offset, uint16_t value)
{
  mem[offset] = value & 0xFF;
  mem[offset + 1] = (value >> 8) & 0xFF;
}

static inline size_t seg_off_to_linear(uint16_t seg, uint16_t off)
{
  return (seg * 16) + off;
}

/********************************************************************/

static int load_exe(
        const char       *fname,
        unsigned char    *mem,
        x86_regs         *regs
)
{
  exehdr__s  hdr;
  size_t     binsize;
  size_t     i;
  uint16_t   off[2];
  FILE      *fp;
  psp__s    *psp;
  uint16_t  *patch;
  size_t     offset;
  
  assert(fname != NULL);
  assert(regs  != NULL);
  
  memset(&mem[MEM_ENV],0,256);
  psp = (psp__s *)&mem[MEM_PSP];
  
  memset(psp,0,256);
  psp->warmboot[0]   = 0xCD;
  psp->warmboot[1]   = 0x20;
  psp->oldmscall_jmp = 0x9A;
  psp->oldmscall_off = offsetof(psp__s,mscall);
  psp->oldmscall_seg = SEG_PSP;
  psp->termaddr[0]   = 129;
  psp->termaddr[1]   = SEG_PSP;
  psp->ctrlcaddr[0]  = 130;
  psp->ctrlcaddr[1]  = SEG_PSP;
  psp->erroraddr[0]  = 131;
  psp->erroraddr[1]  = SEG_PSP;
  psp->envp          = SEG_ENV;
  psp->mscall[0]     = 0xCD;
  psp->mscall[1]     = 0x21;
  psp->mscall[2]     = 0xCB;
  
  /* Dummy interrupt handlers */
  mem[MEM_PSP + 129] = 0xCF; /* IRET */
  mem[MEM_PSP + 130] = 0xCF; /* IRET */
  mem[MEM_PSP + 131] = 0xCF; /* IRET */
  
  fp = fopen(fname,"rb");
  if (fp == NULL)
  {
    fprintf(stderr,"fopen(\"%s\") = %s\n",fname,strerror(errno));
    return EXIT_FAILURE;
  }
  
  fread(&hdr,sizeof(hdr),1,fp);
  if ((hdr.magic[0] != 0x4D) || (hdr.magic[1] != 0x5A))
  {
    fprintf(stderr,"%s: not an EXE file\n",fname);
    fclose(fp);
    return EXIT_FAILURE;
  }
  
  offset = hdr.hdrpara * 16;
  
  if (hdr.lastpagesize == 0)
    binsize = hdr.filepages * 512;
  else
    binsize = (hdr.filepages - 1) * 512 + hdr.lastpagesize;
  
  binsize -= offset;
  
  fseek(fp,offset,SEEK_SET);
  fread(&mem[MEM_LOAD],1,binsize,fp);
  
  /* Process relocations */
  for (i = 0 ; i < hdr.numreloc ; i++)
  {
    fseek(fp,hdr.reltable + i * 4,SEEK_SET);
    fread(off,sizeof(uint16_t),2,fp);
    patch = (uint16_t *)&mem[MEM_LOAD + off[0] + off[1] * 16];
    *patch += SEG_LOAD;
  }
  
  fclose(fp);
  
  /* Initialize registers */
  memset(regs, 0, sizeof(x86_regs));
  regs->cs = SEG_LOAD + hdr.init_cs;
  regs->ip = hdr.init_ip;
  regs->ss = SEG_LOAD + hdr.init_ss;
  regs->sp = hdr.init_sp;
  regs->ds = SEG_PSP;
  regs->es = SEG_PSP;
  regs->ax = 0;
  regs->flags = 0x0200; /* Interrupts enabled */
  
  return EXIT_SUCCESS;
}

/********************************************************************/

static void dos_int21(system__s *sys)
{
  unsigned char *mem = sys->mem;
  uint8_t func = sys->regs.ax >> 8;
  
  switch(func)
  {
    case 0x01: /* Read character with echo */
      {
        int c = getchar();
        if (c != EOF)
        {
          sys->regs.ax = (sys->regs.ax & 0xFF00) | (c & 0xFF);
          if (c != '\n')
            putchar(c);
        }
      }
      break;
      
    case 0x02: /* Write character */
      {
        char c = sys->regs.dx & 0xFF;
        putchar(c);
        fflush(stdout);
        
        /* Handle Racter prompt detection */
        memmove(&sys->prompt[0], &sys->prompt[1], 3);
        sys->prompt[3] = c;
        
        if (sys->prompt[1] == '\r' && sys->prompt[2] == '\n' && sys->prompt[3] == '>')
        {
          sys->input = true;
        }
        else if (c == '\r')
        {
          sys->input = false;
        }
      }
      break;
      
    case 0x09: /* Write string */
      {
        size_t addr = seg_off_to_linear(sys->regs.ds, sys->regs.dx);
        while (mem[addr] != '$')
        {
          putchar(mem[addr]);
          
          /* Handle Racter prompt detection */
          memmove(&sys->prompt[0], &sys->prompt[1], 3);
          sys->prompt[3] = mem[addr];
          
          if (sys->prompt[1] == '\r' && sys->prompt[2] == '\n' && sys->prompt[3] == '>')
          {
            sys->input = true;
          }
          else if (mem[addr] == '\r')
          {
            sys->input = false;
          }
          
          addr++;
        }
        fflush(stdout);
      }
      break;
      
    case 0x0C: /* Clear keyboard buffer and read */
      {
        uint8_t subfunc = sys->regs.ax & 0xFF;
        if (subfunc == 0x01 || subfunc == 0x06 || subfunc == 0x07 || subfunc == 0x08 || subfunc == 0x0A)
        {
          /* Clear any pending input */
          int c;
          struct pollfd pfd = { .fd = 0, .events = POLLIN };
          while (poll(&pfd, 1, 0) > 0)
          {
            c = getchar();
            if (c == EOF) break;
          }
          
          /* Now handle the subfunction */
          sys->regs.ax = (subfunc << 8) | subfunc;
          dos_int21(sys);
        }
      }
      break;
      
    case 0x19: /* Get current drive */
      sys->regs.ax = (sys->regs.ax & 0xFF00) | 0x02; /* C: drive */
      break;
      
    case 0x25: /* Set interrupt vector - ignored */
      break;
      
    case 0x30: /* Get DOS version */
      sys->regs.ax = 0x0005; /* DOS 5.0 */
      sys->regs.bx = 0x0000;
      sys->regs.cx = 0x0000;
      break;
      
    case 0x35: /* Get interrupt vector */
      /* Return dummy values */
      sys->regs.es = 0x0000;
      sys->regs.bx = 0x0000;
      break;
      
    case 0x4C: /* Exit program */
      sys->running = false;
      break;
      
    default:
      fprintf(stderr, "Unhandled DOS INT 21h function: %02X\n", func);
      break;
  }
}

/********************************************************************/

/* Simple x86 instruction decoder and executor */
static void execute_instruction(system__s *sys)
{
  unsigned char *mem = sys->mem;
  size_t ip_addr = seg_off_to_linear(sys->regs.cs, sys->regs.ip);
  uint8_t opcode = mem[ip_addr];
  
  switch(opcode)
  {
    case 0xCD: /* INT instruction */
      {
        uint8_t int_num = mem[ip_addr + 1];
        sys->regs.ip += 2;
        
        if (int_num == 0x21)
        {
          dos_int21(sys);
        }
        else if (int_num == 0x20)
        {
          /* Program termination */
          sys->running = false;
        }
        else
        {
          fprintf(stderr, "Unhandled interrupt: %02X\n", int_num);
        }
      }
      break;
      
    case 0xCF: /* IRET */
      {
        size_t sp_addr = seg_off_to_linear(sys->regs.ss, sys->regs.sp);
        sys->regs.ip = get_word(mem, sp_addr);
        sys->regs.cs = get_word(mem, sp_addr + 2);
        sys->regs.flags = get_word(mem, sp_addr + 4);
        sys->regs.sp += 6;
      }
      break;
      
    case 0xCB: /* RETF */
      {
        size_t sp_addr = seg_off_to_linear(sys->regs.ss, sys->regs.sp);
        sys->regs.ip = get_word(mem, sp_addr);
        sys->regs.cs = get_word(mem, sp_addr + 2);
        sys->regs.sp += 4;
      }
      break;
      
    default:
      fprintf(stderr, "Unhandled opcode at %04X:%04X: %02X\n", 
              sys->regs.cs, sys->regs.ip, opcode);
      sys->running = false;
      break;
  }
}

/********************************************************************/

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: %s file\n", argv[0]);
    exit(2);
  }
  
  setvbuf(stdin, NULL, _IONBF, 0);  
  setvbuf(stdout, NULL, _IONBF, 0);
  atexit(cleanup);
  
  g_sys.mem = malloc(1024 * 1024);
  if (g_sys.mem == NULL)
  {
    perror("malloc()");
    exit(3);
  }
  
  memset(g_sys.mem, 0xCC, 1024 * 1024);
  memset(&g_sys, 0, sizeof(g_sys));
  g_sys.mem = malloc(1024 * 1024);
  g_sys.running = true;
  
  if (load_exe(argv[1], g_sys.mem, &g_sys.regs) != EXIT_SUCCESS)
  {
    exit(4);
  }
  
  fprintf(stderr, "Note: This is a minimal DOS emulator that only supports basic INT 21h functions.\n");
  fprintf(stderr, "It may not run all DOS programs correctly.\n\n");
  
  /* Main execution loop */
  while (g_sys.running)
  {
    if (g_sys.input)
    {
      struct pollfd pfd = { .fd = 0, .events = POLLIN };
      if (poll(&pfd, 1, 0) > 0)
      {
        execute_instruction(&g_sys);
      }
    }
    else
    {
      execute_instruction(&g_sys);
    }
  }
  
  return EXIT_SUCCESS;
}
