# Running Racter with Docker

The original MS-DOS emulator (`original.c`) uses the Linux `vm86()` system call which is only available on x86 Linux systems. Since macOS doesn't support this, we use Docker to run it in a Linux environment.

## Prerequisites

- Docker installed on your system
- The RACTER files in the `RACTER/` directory

## Building and Running

1. **Build the Docker image:**
   ```bash
   ./run-docker.sh
   ```

   Or manually:
   ```bash
   docker build -t racter-dos .
   docker run --rm -it --privileged racter-dos
   ```

2. **Expected Output:**
   ```
   .-----------------------------------------------------,
   |                                                     |
   |            A CONVERSATION WITH RACTER               |
   |                                                     |
   |       COPYRIGHTED BY INRAC CORPORATION, 1984        |
   | PORTIONS COPYRIGHTED BY MICROSOFT CORPORATION, 1982 |
   |                   ...........                       |
   `-----------------------------------------------------' Hello, I'm
   Racter.  Are you Eliza?
   ```

## How it works

- The Docker container runs Ubuntu 20.04 with the necessary development tools
- It compiles the original DOS emulator (`msdos.c`) which uses `vm86()`
- The emulator creates a minimal DOS environment and runs `RACTER.EXE`
- The `--privileged` flag is needed for the `vm86()` system call to work

## Files

- `Dockerfile` - Container definition
- `run-docker.sh` - Build and run script  
- `C/msdos.c` - The working DOS emulator (cleaned up from the original)

## Troubleshooting

If you get permission errors, make sure Docker is running and you have permission to use it:

```bash
sudo docker build -t racter-dos .
sudo docker run --rm -it --privileged racter-dos
```
