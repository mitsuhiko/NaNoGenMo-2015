/************************************************************************
*
* Copyright 2015 by Sean Conner.  All Rights Reserved.
* Modified 2025 for better pipe communication
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

/* Improved portable version with better I/O handling for pipes */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>

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

/* Enhanced x86 registers structure */
typedef struct x86_regs {
  uint32_t eax, ebx, ecx, edx;
  uint32_t esi, edi, ebp, esp;
  uint32_t eip;
  uint32_t eflags;
  uint16_t cs, ds, es, ss, fs, gs;
} x86_regs;

typedef struct system
{
  x86_regs                regs;
  unsigned char          *mem;
  fcb__s                 *fcbs[16];
  FILE                   *fp[16];
  uint16_t                dtaseg;
  uint16_t                dtaoff;
  
  /* Racter prompt detection */
  bool input;
  char prompt[4];
  bool running;
  
  /* Input buffer for better pipe handling */
  char input_buffer[256];
  int input_len;
  int input_pos;
  
  /* Debug mode */
  bool debug;
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
  
  for (int i = 0; i < 16; i++)
  {
    if (g_sys.fp[i] != NULL)
    {
      fclose(g_sys.fp[i]);
      g_sys.fp[i] = NULL;
    }
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

static inline uint32_t get_dword(unsigned char *mem, size_t offset)
{
  return get_word(mem, offset) | ((uint32_t)get_word(mem, offset + 2) << 16);
}

static inline void set_dword(unsigned char *mem, size_t offset, uint32_t value)
{
  set_word(mem, offset, value & 0xFFFF);
  set_word(mem, offset + 2, (value >> 16) & 0xFFFF);
}

static inline size_t seg_off_to_linear(uint16_t seg, uint16_t off)
{
  return ((size_t)seg * 16) + off;
}

/********************************************************************/

static void mkfilename(char *fname, fcb__s *fcb)
{
  int i;
  int c;
  
  for(i = 0 , c = 0 ; (i < 8) && (fcb->name[i] != ' ') ; i++ , c++)
    fname[c] = fcb->name[i];
    
  if (fcb->ext[0] != ' ')
  {
    fname[c++] = '.';
    for(i = 0 ; (i < 3) && (fcb->ext[i] != ' ') ; i++ , c++)
      fname[c] = fcb->ext[i];
  }
  
  fname[c] = '\0';
}

/********************************************************************/

static int find_fcb(system__s *sys, fcb__s *fcb)
{
  for (size_t i = 0 ; i < 16 ; i++)
    if (sys->fcbs[i] == fcb)
      return i;
  return -1;
}

/********************************************************************/

static int open_file(system__s *sys, fcb__s *fcb, bool create)
{
  char fname[13];
  int  idx;
  int  i;
  
  mkfilename(fname, fcb);
  
  if (create)
  {
    idx = find_fcb(sys, fcb);
    if (idx == -1)
    {
      for (i = 0 ; i < 16 ; i++)
        if (sys->fcbs[i] == NULL)
          break;
      idx = i;
    }
    
    sys->fp[idx] = fopen(fname, "w+b");
  }
  else
  {
    for (i = 0 ; i < 16 ; i++)
      if (sys->fcbs[i] == NULL)
        break;
    idx = i;
    sys->fp[idx] = fopen(fname, "r+b");
    if (sys->fp[idx] == NULL)
      sys->fp[idx] = fopen(fname, "rb");
  }
  
  if (sys->fp[idx] == NULL)
  {
    return 255;
  }
  
  sys->fcbs[idx] = fcb;
  fcb->cblock    = 0;
  fcb->crecnum   = 0;
  fcb->recsize   = 128;
  
  if (!create)
  {
    struct stat buf;
    fstat(fileno(sys->fp[idx]), &buf);
    fcb->size = buf.st_size;
  }
  else
    fcb->size = 0;
    
  fcb->drive = 3;  /* C: drive */
  return 0;
}

/********************************************************************/

static int load_program(
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
  struct stat st;
  
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
  
  /* Get file size */
  fstat(fileno(fp), &st);
  
  /* Check if it's an EXE file */
  fread(&hdr,sizeof(hdr),1,fp);
  rewind(fp);
  
  if ((hdr.magic[0] == 0x4D) && (hdr.magic[1] == 0x5A))
  {
    /* EXE file */
    fread(&hdr,sizeof(hdr),1,fp);
    
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
    
    /* Initialize registers for EXE */
    memset(regs, 0, sizeof(x86_regs));
    regs->cs = SEG_LOAD + hdr.init_cs;
    regs->eip = hdr.init_ip;
    regs->ss = SEG_LOAD + hdr.init_ss;
    regs->esp = hdr.init_sp;
    regs->ds = SEG_PSP;
    regs->es = SEG_PSP;
    regs->eax = 0;
    regs->eflags = 0x0200; /* Interrupts enabled */
  }
  else
  {
    /* COM file - load at offset 0x100 in PSP segment */
    binsize = st.st_size;
    if (binsize > 65536 - 256)
    {
      fprintf(stderr,"%s: COM file too large\n",fname);
      fclose(fp);
      return EXIT_FAILURE;
    }
    
    /* Load COM file at PSP:0100h */
    fread(&mem[MEM_PSP + 0x100], 1, binsize, fp);
    
    /* Initialize registers for COM */
    memset(regs, 0, sizeof(x86_regs));
    regs->cs = SEG_PSP;
    regs->ds = SEG_PSP;
    regs->es = SEG_PSP;
    regs->ss = SEG_PSP;
    regs->eip = 0x100;  /* COM files start at offset 0x100 */
    regs->esp = 0xFFFE; /* Stack at top of segment */
    regs->eax = 0;
    regs->eflags = 0x0200; /* Interrupts enabled */
  }
  
  fclose(fp);
  
  /* Set up DTA (Disk Transfer Area) */
  g_sys.dtaseg = SEG_PSP;
  g_sys.dtaoff = 0x80;
  
  return EXIT_SUCCESS;
}

/********************************************************************/

static void handle_prompt_detection(system__s *sys, char c)
{
  /* Handle Racter prompt detection */
  memmove(&sys->prompt[0], &sys->prompt[1], 3);
  sys->prompt[3] = c;
  
  if (sys->prompt[1] == '\r' && sys->prompt[2] == '\n' && sys->prompt[3] == '>')
  {
    sys->input = true;
    fflush(stdout);
  }
  else if (c == '\r')
  {
    sys->input = false;
  }
}

/********************************************************************/

static int read_buffered_input(system__s *sys)
{
  if (sys->input_pos < sys->input_len)
  {
    return sys->input_buffer[sys->input_pos++];
  }
  
  /* Check if input is available without blocking */
  struct pollfd pfd = { .fd = 0, .events = POLLIN };
  if (poll(&pfd, 1, 0) > 0)
  {
    sys->input_len = read(0, sys->input_buffer, sizeof(sys->input_buffer) - 1);
    if (sys->input_len > 0)
    {
      sys->input_pos = 0;
      return sys->input_buffer[sys->input_pos++];
    }
  }
  
  return -1;
}

/********************************************************************/

static void dos_int21(system__s *sys)
{
  unsigned char *mem = sys->mem;
  uint8_t func = (sys->regs.eax >> 8) & 0xFF;
  fcb__s *fcb;
  size_t idx;
  int handle;
  
  if (sys->debug)
    fprintf(stderr, "DOS INT 21h AH=%02X\n", func);
  
  switch(func)
  {
    case 0x01: /* Read character with echo */
      {
        int c = read_buffered_input(sys);
        if (c >= 0)
        {
          sys->regs.eax = (sys->regs.eax & 0xFF00) | (c & 0xFF);
          if (c != '\n')
            putchar(c);
          fflush(stdout);
        }
        else
        {
          /* No input available, try to wait a bit for piped input */
          usleep(1000); /* 1ms */
          c = read_buffered_input(sys);
          if (c >= 0)
          {
            sys->regs.eax = (sys->regs.eax & 0xFF00) | (c & 0xFF);
            if (c != '\n')
              putchar(c);
            fflush(stdout);
          }
          else
          {
            /* Still no input, return without blocking */
            sys->regs.eax = (sys->regs.eax & 0xFF00);
          }
        }
      }
      break;
      
    case 0x02: /* Write character */
      {
        char c = sys->regs.edx & 0xFF;
        putchar(c);
        handle_prompt_detection(sys, c);
      }
      break;
      
    case 0x06: /* Direct console I/O */
      {
        uint8_t dl = sys->regs.edx & 0xFF;
        if (dl == 0xFF) /* Input */
        {
          int c = read_buffered_input(sys);
          if (c >= 0)
          {
            sys->regs.eax = (sys->regs.eax & 0xFF00) | (c & 0xFF);
            sys->regs.eflags &= ~0x40; /* Clear ZF */
          }
          else
          {
            sys->regs.eflags |= 0x40; /* Set ZF */
          }
        }
        else /* Output */
        {
          putchar(dl);
          handle_prompt_detection(sys, dl);
        }
      }
      break;
      
    case 0x09: /* Write string */
      {
        size_t addr = seg_off_to_linear(sys->regs.ds, sys->regs.edx & 0xFFFF);
        while (mem[addr] != '$')
        {
          putchar(mem[addr]);
          handle_prompt_detection(sys, mem[addr]);
          addr++;
        }
      }
      break;
      
    case 0x0C: /* Clear keyboard buffer and read */
      {
        uint8_t subfunc = sys->regs.eax & 0xFF;
        
        /* Clear input buffer */
        sys->input_pos = 0;
        sys->input_len = 0;
        
        /* Drain any pending input */
        struct pollfd pfd = { .fd = 0, .events = POLLIN };
        while (poll(&pfd, 1, 0) > 0)
        {
          char dummy[256];
          if (read(0, dummy, sizeof(dummy)) <= 0)
            break;
        }
        
        /* Now handle the subfunction */
        if (subfunc == 0x01 || subfunc == 0x06 || subfunc == 0x07 || 
            subfunc == 0x08 || subfunc == 0x0A)
        {
          sys->regs.eax = (subfunc << 8) | subfunc;
          dos_int21(sys);
        }
      }
      break;
      
    /* FCB File operations */
    case 0x0F: /* Open file using FCB */
      idx = seg_off_to_linear(sys->regs.ds, sys->regs.edx & 0xFFFF);
      fcb = (fcb__s *)&sys->mem[idx];
      sys->regs.eax = (sys->regs.eax & 0xFF00) | open_file(sys, fcb, false);
      break;
      
    case 0x10: /* Close file using FCB */
      idx = seg_off_to_linear(sys->regs.ds, sys->regs.edx & 0xFFFF);
      fcb = (fcb__s *)&sys->mem[idx];
      handle = find_fcb(sys, fcb);
      if (handle >= 0)
      {
        fclose(sys->fp[handle]);
        sys->fp[handle] = NULL;
        sys->fcbs[handle] = NULL;
        sys->regs.eax = (sys->regs.eax & 0xFF00);
      }
      else
        sys->regs.eax = (sys->regs.eax & 0xFF00) | 0xFF;
      break;
      
    case 0x14: /* Sequential read using FCB */
      idx = seg_off_to_linear(sys->regs.ds, sys->regs.edx & 0xFFFF);
      fcb = (fcb__s *)&sys->mem[idx];
      handle = find_fcb(sys, fcb);
      if (handle >= 0)
      {
        size_t dta = seg_off_to_linear(sys->dtaseg, sys->dtaoff);
        size_t nread = fread(&sys->mem[dta], 1, fcb->recsize, sys->fp[handle]);
        if (nread == fcb->recsize)
        {
          fcb->crecnum++;
          sys->regs.eax = (sys->regs.eax & 0xFF00);
        }
        else
          sys->regs.eax = (sys->regs.eax & 0xFF00) | 0x01;
      }
      else
        sys->regs.eax = (sys->regs.eax & 0xFF00) | 0xFF;
      break;
      
    case 0x16: /* Create file using FCB */
      idx = seg_off_to_linear(sys->regs.ds, sys->regs.edx & 0xFFFF);
      fcb = (fcb__s *)&sys->mem[idx];
      sys->regs.eax = (sys->regs.eax & 0xFF00) | open_file(sys, fcb, true);
      break;
      
    case 0x19: /* Get current drive */
      sys->regs.eax = (sys->regs.eax & 0xFF00) | 0x02; /* C: drive */
      break;
      
    case 0x1A: /* Set DTA */
      sys->dtaseg = sys->regs.ds;
      sys->dtaoff = sys->regs.edx & 0xFFFF;
      break;
      
    case 0x25: /* Set interrupt vector - ignored */
      break;
      
    case 0x30: /* Get DOS version */
      sys->regs.eax = 0x0005; /* DOS 5.0 */
      sys->regs.ebx = 0x0000;
      sys->regs.ecx = 0x0000;
      break;
      
    case 0x35: /* Get interrupt vector */
      /* Return dummy values */
      sys->regs.es = 0x0000;
      sys->regs.ebx = 0x0000;
      break;
      
    case 0x4C: /* Exit program */
      sys->running = false;
      break;
      
    default:
      if (sys->debug)
        fprintf(stderr, "Unhandled DOS INT 21h function: %02X\n", func);
      break;
  }
}

/********************************************************************/

/* Enhanced x86 instruction decoder and executor */
static bool execute_instruction(system__s *sys)
{
  unsigned char *mem = sys->mem;
  size_t ip_addr = seg_off_to_linear(sys->regs.cs, sys->regs.eip & 0xFFFF);
  uint8_t opcode = mem[ip_addr];
  bool executed = true;
  
  if (sys->debug)
    fprintf(stderr, "Execute: %04X:%04X: %02X\n", sys->regs.cs, (uint16_t)sys->regs.eip, opcode);
  
  /* Enhanced instruction handling */
  switch(opcode)
  {
    case 0x90: /* NOP */
      sys->regs.eip++;
      break;
      
    /* MOV immediate to register instructions */
    case 0xB0: /* MOV AL, imm8 */
      sys->regs.eax = (sys->regs.eax & 0xFFFFFF00) | mem[ip_addr + 1];
      sys->regs.eip += 2;
      break;
      
    case 0xB1: /* MOV CL, imm8 */
      sys->regs.ecx = (sys->regs.ecx & 0xFFFFFF00) | mem[ip_addr + 1];
      sys->regs.eip += 2;
      break;
      
    case 0xB2: /* MOV DL, imm8 */
      sys->regs.edx = (sys->regs.edx & 0xFFFFFF00) | mem[ip_addr + 1];
      sys->regs.eip += 2;
      break;
      
    case 0xB3: /* MOV BL, imm8 */
      sys->regs.ebx = (sys->regs.ebx & 0xFFFFFF00) | mem[ip_addr + 1];
      sys->regs.eip += 2;
      break;
      
    case 0xB4: /* MOV AH, imm8 */
      sys->regs.eax = (sys->regs.eax & 0xFFFF00FF) | ((uint32_t)mem[ip_addr + 1] << 8);
      sys->regs.eip += 2;
      break;
      
    case 0xB5: /* MOV CH, imm8 */
      sys->regs.ecx = (sys->regs.ecx & 0xFFFF00FF) | ((uint32_t)mem[ip_addr + 1] << 8);
      sys->regs.eip += 2;
      break;
      
    case 0xB6: /* MOV DH, imm8 */
      sys->regs.edx = (sys->regs.edx & 0xFFFF00FF) | ((uint32_t)mem[ip_addr + 1] << 8);
      sys->regs.eip += 2;
      break;
      
    case 0xB7: /* MOV BH, imm8 */
      sys->regs.ebx = (sys->regs.ebx & 0xFFFF00FF) | ((uint32_t)mem[ip_addr + 1] << 8);
      sys->regs.eip += 2;
      break;
      
    case 0xB8: /* MOV EAX/AX, imm16/32 */
      sys->regs.eax = get_word(mem, ip_addr + 1);
      sys->regs.eip += 3;
      break;
      
    case 0xB9: /* MOV ECX/CX, imm16/32 */
      sys->regs.ecx = get_word(mem, ip_addr + 1);
      sys->regs.eip += 3;
      break;
      
    case 0xBA: /* MOV EDX/DX, imm16/32 */
      sys->regs.edx = get_word(mem, ip_addr + 1);
      sys->regs.eip += 3;
      break;
      
    case 0xBB: /* MOV EBX/BX, imm16/32 */
      sys->regs.ebx = get_word(mem, ip_addr + 1);
      sys->regs.eip += 3;
      break;
      
    case 0xBC: /* MOV ESP/SP, imm16/32 */
      sys->regs.esp = get_word(mem, ip_addr + 1);
      sys->regs.eip += 3;
      break;
      
    case 0xBD: /* MOV EBP/BP, imm16/32 */
      sys->regs.ebp = get_word(mem, ip_addr + 1);
      sys->regs.eip += 3;
      break;
      
    case 0xBE: /* MOV ESI/SI, imm16/32 */
      sys->regs.esi = get_word(mem, ip_addr + 1);
      sys->regs.eip += 3;
      break;
      
    case 0xBF: /* MOV EDI/DI, imm16/32 */
      sys->regs.edi = get_word(mem, ip_addr + 1);
      sys->regs.eip += 3;
      break;
      
    /* MOV register to register */
    case 0x88: /* MOV r/m8, r8 */
      {
        uint8_t modrm = mem[ip_addr + 1];
        uint8_t mod = (modrm >> 6) & 3;
        uint8_t reg = (modrm >> 3) & 7;
        uint8_t rm = modrm & 7;
        
        if (mod == 3) /* Register to register */
        {
          uint8_t src_val = 0;
          switch(reg)
          {
            case 0: src_val = sys->regs.eax & 0xFF; break; /* AL */
            case 1: src_val = sys->regs.ecx & 0xFF; break; /* CL */
            case 2: src_val = sys->regs.edx & 0xFF; break; /* DL */
            case 3: src_val = sys->regs.ebx & 0xFF; break; /* BL */
            case 4: src_val = (sys->regs.eax >> 8) & 0xFF; break; /* AH */
            case 5: src_val = (sys->regs.ecx >> 8) & 0xFF; break; /* CH */
            case 6: src_val = (sys->regs.edx >> 8) & 0xFF; break; /* DH */
            case 7: src_val = (sys->regs.ebx >> 8) & 0xFF; break; /* BH */
          }
          
          switch(rm)
          {
            case 0: sys->regs.eax = (sys->regs.eax & 0xFFFFFF00) | src_val; break; /* AL */
            case 1: sys->regs.ecx = (sys->regs.ecx & 0xFFFFFF00) | src_val; break; /* CL */
            case 2: sys->regs.edx = (sys->regs.edx & 0xFFFFFF00) | src_val; break; /* DL */
            case 3: sys->regs.ebx = (sys->regs.ebx & 0xFFFFFF00) | src_val; break; /* BL */
            case 4: sys->regs.eax = (sys->regs.eax & 0xFFFF00FF) | ((uint32_t)src_val << 8); break; /* AH */
            case 5: sys->regs.ecx = (sys->regs.ecx & 0xFFFF00FF) | ((uint32_t)src_val << 8); break; /* CH */
            case 6: sys->regs.edx = (sys->regs.edx & 0xFFFF00FF) | ((uint32_t)src_val << 8); break; /* DH */
            case 7: sys->regs.ebx = (sys->regs.ebx & 0xFFFF00FF) | ((uint32_t)src_val << 8); break; /* BH */
          }
        }
        sys->regs.eip += 2;
      }
      break;
      
    /* Compare instructions */
    case 0x3C: /* CMP AL, imm8 */
      {
        uint8_t al = sys->regs.eax & 0xFF;
        uint8_t imm = mem[ip_addr + 1];
        uint16_t result = al - imm;
        
        /* Set flags */
        sys->regs.eflags &= ~(0x0001 | 0x0040 | 0x0080); /* Clear CF, ZF, SF */
        if (result == 0) sys->regs.eflags |= 0x0040; /* ZF */
        if (result & 0x8000) sys->regs.eflags |= 0x0080; /* SF */
        if (al < imm) sys->regs.eflags |= 0x0001; /* CF */
        
        sys->regs.eip += 2;
      }
      break;
      
    /* Jump instructions */
    case 0x75: /* JNZ/JNE rel8 */
      {
        int8_t rel = (int8_t)mem[ip_addr + 1];
        sys->regs.eip += 2;
        if (!(sys->regs.eflags & 0x0040)) /* ZF clear */
        {
          sys->regs.eip = (sys->regs.eip + rel) & 0xFFFF;
        }
      }
      break;
      
    case 0x74: /* JZ/JE rel8 */
      {
        int8_t rel = (int8_t)mem[ip_addr + 1];
        sys->regs.eip += 2;
        if (sys->regs.eflags & 0x0040) /* ZF set */
        {
          sys->regs.eip = (sys->regs.eip + rel) & 0xFFFF;
        }
      }
      break;
      
    case 0xEB: /* JMP rel8 */
      {
        int8_t rel = (int8_t)mem[ip_addr + 1];
        sys->regs.eip = (sys->regs.eip + 2 + rel) & 0xFFFF;
      }
      break;
      
    /* Push/Pop instructions */
    case 0x50: /* PUSH EAX/AX */
      {
        size_t sp_addr = seg_off_to_linear(sys->regs.ss, (sys->regs.esp - 2) & 0xFFFF);
        set_word(mem, sp_addr, sys->regs.eax & 0xFFFF);
        sys->regs.esp = (sys->regs.esp - 2) & 0xFFFF;
        sys->regs.eip++;
      }
      break;
      
    case 0x58: /* POP EAX/AX */
      {
        size_t sp_addr = seg_off_to_linear(sys->regs.ss, sys->regs.esp & 0xFFFF);
        sys->regs.eax = (sys->regs.eax & 0xFFFF0000) | get_word(mem, sp_addr);
        sys->regs.esp = (sys->regs.esp + 2) & 0xFFFF;
        sys->regs.eip++;
      }
      break;
      
    case 0xCD: /* INT instruction */
      {
        uint8_t int_num = mem[ip_addr + 1];
        sys->regs.eip += 2;
        
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
          if (sys->debug)
            fprintf(stderr, "Unhandled interrupt: %02X\n", int_num);
        }
      }
      break;
      
    case 0xCF: /* IRET */
      {
        size_t sp_addr = seg_off_to_linear(sys->regs.ss, sys->regs.esp & 0xFFFF);
        sys->regs.eip = get_word(mem, sp_addr);
        sys->regs.cs = get_word(mem, sp_addr + 2);
        sys->regs.eflags = get_word(mem, sp_addr + 4);
        sys->regs.esp += 6;
      }
      break;
      
    case 0xCB: /* RETF */
      {
        size_t sp_addr = seg_off_to_linear(sys->regs.ss, sys->regs.esp & 0xFFFF);
        sys->regs.eip = get_word(mem, sp_addr);
        sys->regs.cs = get_word(mem, sp_addr + 2);
        sys->regs.esp += 4;
      }
      break;
      
    case 0xC3: /* RET */
      {
        size_t sp_addr = seg_off_to_linear(sys->regs.ss, sys->regs.esp & 0xFFFF);
        sys->regs.eip = get_word(mem, sp_addr);
        sys->regs.esp += 2;
      }
      break;
      
    default:
      /* For unhandled opcodes, skip the instruction and hope for the best */
      if (sys->debug)
      {
        fprintf(stderr, "Unhandled opcode at %04X:%04X: %02X\n", 
                sys->regs.cs, (uint16_t)sys->regs.eip, opcode);
      }
      sys->regs.eip++;
      executed = false;
      break;
  }
  
  return executed;
}

/********************************************************************/

int main(int argc, char *argv[])
{
  int cycles_without_io = 0;
  
  if (argc < 2)
  {
    fprintf(stderr, "usage: %s file [-d]\n", argv[0]);
    exit(2);
  }
  
  if (argc > 2 && strcmp(argv[2], "-d") == 0)
    g_sys.debug = true;
  
  /* Set up non-blocking I/O */
  int flags = fcntl(0, F_GETFL, 0);
  fcntl(0, F_SETFL, flags | O_NONBLOCK);
  
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
  g_sys.running = true;
  g_sys.input = false;
  g_sys.input_len = 0;
  g_sys.input_pos = 0;
  
  if (load_program(argv[1], g_sys.mem, &g_sys.regs) != EXIT_SUCCESS)
  {
    exit(4);
  }
  
  if (!g_sys.debug)
  {
    fprintf(stderr, "Note: This is a minimal DOS emulator for Racter.\n");
    fprintf(stderr, "It implements just enough to handle basic I/O.\n\n");
  }
  
  /* Main execution loop */
  while (g_sys.running)
  {
    bool executed = execute_instruction(&g_sys);
    
    /* If we didn't execute a real instruction, we might be stuck */
    if (!executed)
    {
      cycles_without_io++;
      if (cycles_without_io > 10000)
      {
        /* We're probably stuck in a loop, try to find the next INT 21h */
        size_t addr = seg_off_to_linear(g_sys.regs.cs, g_sys.regs.eip & 0xFFFF);
        bool found = false;
        
        for (int i = 0; i < 100 && !found; i++)
        {
          if (g_sys.mem[addr + i] == 0xCD && g_sys.mem[addr + i + 1] == 0x21)
          {
            g_sys.regs.eip += i;
            found = true;
            cycles_without_io = 0;
          }
        }
        
        if (!found)
        {
          fprintf(stderr, "Program appears stuck, terminating\n");
          break;
        }
      }
    }
    else
    {
      cycles_without_io = 0;
    }
    
    /* Give other processes a chance to run */
    if (cycles_without_io > 100)
    {
      usleep(1000); /* 1ms sleep */
    }
  }
  
  return EXIT_SUCCESS;
}
