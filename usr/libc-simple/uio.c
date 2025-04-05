// minimal user io lib 
// a thin layer over /dev/XXX and /procfs/XXX
#include "user.h"

#define LINESIZE 128    

//quest: kungfu
// return 0 on success
// cf: kernel code: procfs_parse_fbctl()
int config_fbctl(int w, int d, int vw, int vh, int offx, int offy) {
    char buf[LINESIZE];
    int n, fbctl; 

    if ((fbctl = open("/proc/fbctl", O_RDWR)) <=0) return -1; 

     
    /* STUDENT_TODO: your code here */

    // printf("write returns %d\n", n);

    close(fbctl);  // close it so flush the writes to the kernel
    return !(n>0); 
}

// 0 on success
// not all commands use 5 args. kernel will ignore unused args
// cf kernel/sf.c procfs_parse_fbctl0()
int config_fbctl0(int cmd, int x, int y, int w, int h, int zorder, int trans) {
    char line[64]; 
    int len1; 

    int fbctl0 = open("/proc/fbctl0", O_RDWR);
    if (fbctl0 <= 0) {printf("open fbctl0 err\n"); return -1;}
    
    sprintf(line, "%d %d %d %d %d %d %d\n",cmd,x,y,w,h,zorder,trans); 
    len1 = strlen(line); 
    if ((len1 = write(fbctl0, line, len1)) < 0) {
        printf("write to fbctl0 failed with %d. shouldn't happen", len1);
        exit(1);
    }
    close(fbctl0);   // flush
    return 0; 
}

// return 0 on success. nargs: # of args parsed
// quest: kungfu
int read_dispinfo(int dispinfo[MAX_DISP_ARGS], int *nargs) {
    char buf[LINESIZE], *s;
    int n = 0; 
    int dp = 0; 

    if ((dp = open("/proc/dispinfo", O_RDONLY)) <=0) return -1; 

    // read a line from /proc/dispinfo to buf
    /* STUDENT_TODO: your code here */

    // parse the 1st line from /proc/dispinfo as a list of int args... 
    for (s = buf, *nargs=0; s < buf + n; s++) {
        if (*s == '\n' || *s == '\0')
            break;
        if ('0' <= *s && *s <= '9') {  // reach the 1st char (e.g. '1') in a number (e.g. "123")
             
            /* STUDENT_TODO: your code here */
            // printf("got arg %d\n", dispinfo[nargs]); // debugging
        }
    }    
    // line 2 and later ignored 
    close(dp);  
    return 0; 
}

// read a line from /dev/events and parse it into key events
// line format: [kd|ku] 0x12
// "events": fd for /dev/events
// return: 0 on success 
// quest: mario with inputs
int read_kb_event(int events, int *evtype, unsigned int *scancode) {
    int n; 
    char buf[LINESIZE], *s;

    *evtype = INVALID; *scancode = 0; //invalid

    n = read(events, buf, LINESIZE); if (n<=0) return -1; 
    // printf("%s: %s\n", __func__, buf);
    s=buf;
    // below: set event type
    if (buf[0]=='k' && buf[1]=='d') {
      /* STUDENT_TODO: your code here */
      *evtype = KEYDOWN;
    } else if (buf[0]=='k' && buf[1]=='u') {
      /* STUDENT_TODO: your code here */
      *evtype = KEYUP;
    } 
    s += 2; while (*s==' ') s++; 
    if (s[0]=='0' && s[1]=='x')
      *scancode = atoi16(s); 
    return 0; 
}

