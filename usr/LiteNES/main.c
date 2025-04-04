/*
---- Below: comment from NJU OS project ---- 

LiteNES originates from Stanislav Yaglo's mynes project:
  https://github.com/yaglo/mynes

LiteNES is a "more portable" version of mynes.
  all system(library)-dependent code resides in "hal.c" and "main.c"
  only depends on libc's memory moving utilities.

How does the emulator work?
  1) read file name at argv[1]
  2) load the rom file into array rom
  3) call fce_load_rom(rom) for parsing
  4) call fce_init for emulator initialization
  5) call fce_run(), which is a non-exiting loop simulating the NES system
  6) when SIGINT signal is received, it kills itself (TBD)
*/

#include "fce.h"   
#include "common.h"   
#include "../user.h"

#define stderr 2

#include "mario-rom.h"    // built-in rom buffer with Super Mario

// quest: kungfu
int main(int argc, char *argv[])
{
    int fd; 

    if (argc != 2) {
        fprintf(stderr, "Usage: mynes romfile.nes\n");
        fprintf(stderr, "no rom file specified. use built-in rom\n"); 
        goto load; 
    }
    
    // if arg[1] specified, use it as a rom filename and load the rom 
    // to the memory buffer
     
    /* STUDENT_TODO: your code here */
    if ((fd = open(argv[1], O_RDONLY)) < 0) {
      fprintf(stderr, "Cannot open rom file %s\n", argv[1]);
      exit(1);
    }

    if (read(fd, rom, sizeof(rom)) <= 0) {
      fprintf(stderr, "Cannot read rom file %s\n", argv[1]);
      close(fd);
      exit(1);
    }
    close(fd);  

    printf("open rom...ok\n"); 
load: 
    if (fce_load_rom(rom) != 0) {
        fprintf(stderr, "Invalid or unsupported rom.\n");
        exit(1);
    }
    printf("load rom...ok\n"); 
    // signal(SIGINT, do_exit);

    // well, more GB than NES
    printf("   |          ________        |\n");
    printf("   |    .    (Nintendo)       |\n");
    printf("   |  _|w|_   ''''''''   .-.  |\n");
    printf("   |-[a   d]-       .-. ( k ) |\n");
    printf("   |   |s|         ( j ) '-'  |\n");
    printf("   |    '           '-'       |\n");
    printf("   |          ___   ___       |\n");
    printf("   |           (u) (i)        |\n");
    printf("   |        SELECT START      |\n");
    fce_init();

    printf("running fce \n"); 
    fce_run();
    return 0;
}
