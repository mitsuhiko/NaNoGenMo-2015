# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a NaNoGenMo 2015 project that generates novels by having two AI chatbots converse: ELIZA (psychotherapist) and Racter (artificial insanity). The project includes a custom MS-DOS emulator to run the proprietary Racter program on Unix systems.

## Build Commands

### MS-DOS Emulator
```bash
cd C/
make
```
This builds `msdos` which works on macOS and modern systems with improved I/O handling for pipes.

### Running the Conversation
```bash
./couch.lua  # Assumes Racter files in /tmp/racter/
```

## Architecture

### Core Components

1. **MS-DOS Emulator (C/msdos.c)**
   - Minimal DOS environment using Linux vm86() system call
   - Memory segments: ENV (0x1000), PSP (0x2000), LOAD (0x2010)
   - Implements DOS INT 21h services for file I/O and console operations
   - Special handling for Racter's "CR LF >" prompt format

2. **Orchestrator (couch.lua)**
   - Creates bidirectional pipes between Eliza and Racter
   - Manages process forking and signal handling
   - Parses Racter's output waiting for '>' prompts
   - Handles conversation flow and deadlock detection

3. **Eliza Implementation (eliza.lua)**
   - Pattern-based response system using LPeg
   - Keyword matching with response templates
   - Pronoun swapping (I→you, you→I, etc.)
   - Conversation history to avoid repetition

### Communication Flow
```
couch.lua
    ├── Creates pipes
    ├── Forks Eliza process (eliza.lua)
    ├── Forks Racter process (C/msdos /tmp/racter/RACTER.EXE)
    └── Manages bidirectional message passing
```

## Requirements

- **Racter Files**: Place MS-DOS Racter distribution in `/tmp/racter/` including:
  - RACTER.EXE and all .RAC/.C files (see README for complete list)
- **Lua Dependencies** (from https://github.com/spc476/lua-conmanorg):
  - org.conman.{syslog, errno, fsys, process, signal, math}
  - LPeg

## Known Issues

- Frequent deadlocks after ~4,000 words due to pipe race conditions
- The MS-DOS emulator (`msdos`) implements basic DOS functions needed for Racter with enhanced pipe I/O handling
- Project outputs in novel/ directory (4 files, ~16k words total)

## Key Implementation Details

- Racter prompt detection: waits for exact "CR LF >" sequence
- Eliza uses 1978 BASIC version patterns (ELIZA.BAS as reference)
- Alternative Eliza implementations (eliza2.lua, gen.lua) explore 1965 script format but are incomplete