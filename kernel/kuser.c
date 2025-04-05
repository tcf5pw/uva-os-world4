/* 
    the kernel launcher for user tasks, e.g. sh or snes

    kuser tasks, compiled into the kernel, but executed at EL0 and in their own
    VA. besides syscalls, CANNOT call any kernel functions -- otherwise will
    trigger memory protection error

    in general, the programming environment is very limited, b/c everything
    needs to be re-implemented (e.g. printf())

    So just as a proof-of-concept demonstrating EL and VM concepts. Beyond that,
    nothing much to be done here. The real fun stuffs have to be standalone user
    programs loaded via exec() and linked to real user libs.

    the tricky part is that 
    1. the symbols referenced must be compiled into this unit, between
            user_begin/user_end e.g. cannot call a kernel func defined outside.
            cannot ref to a const string (?) which may also be linked outside
    2. all references to the symbols must be PC-relative. cannot be absolute
        address. this is even harder as we often dont have such a fine control
        of C compiler. Thus, -O0 works often but -O2 will break things. if we
        write ASM, we seem to lack control of where linker put things.
        therefore, linker often complains that the offsets cannot be encoded.
        cf stawged/testuser_launch.S and user-test.c    
 */

#include "kuser_sys.h"
#include "fcntl.h"

static void user_delay (unsigned long cnt) {
	volatile unsigned long c = cnt; 
	while (c)
		c--; 
}

// cannot call kernel's strlen
 __attribute__((unused)) static unsigned int
user_strlen(const char *s)    
{
  int n;
  for(n = 0; s[n]; n++)
    ;
  return n;
}

void print_to_console(char *msg) {
	call_sys_write(1 /*stdout*/, msg, user_strlen(msg)); 
  // call_sys_write(1 /*stdout*/, msg, strlen(msg));     // WILL FAIL. can be used for debugging 
}

void loop(char* str)
{
	char buf[2] = {""};
	while (1){
		for (int i = 0; i < 5; i++){
			buf[0] = str[i];
			call_sys_write(0, buf, 1);
			user_delay(1000000);
		}
	}
}

void loop1(char *str) {
	for (;;)
	// for (int i = 0; i < 3; i++)
		print_to_console(str);
}

void ls(char *path);

#define USER_PROGRAM1(name, arg0, arg1)  \
    char path[] = {name};            \
    char argv0[] = {arg0};            \
    char argv1[] = {arg1};            \
    char *argv[] = {argv0, argv1, 0};

#define USER_PROGRAM2(name, arg0, arg1, arg2)  \
    char path[] = {name};            \
    char argv0[] = {arg0};            \
    char argv1[] = {arg1};            \
    char argv2[] = {arg2};            \
    char *argv[] = {argv0, argv1, argv2, 0};

#define USER_PROGRAM3(name, arg0, arg1, arg2, arg3)  \
    char path[] = {name};            \
    char argv0[] = {arg0};            \
    char argv1[] = {arg1};            \
    char argv2[] = {arg2};            \
    char argv3[] = {arg3};            \
    char *argv[] = {argv0, argv1, argv2, argv3, 0};

//quest: shell
/* The "launcher" for standalone user programs */
void user_process() {
    // below won't work as "arg0"	const string will be linked to kernel va.
    // exec() expecting user va will fail to "copyin" them
    // char *argv[] = {"arg0", "arg1", 0};
    // char *argv[] = {arg0, arg1, arg2, 0};
	// char *argv[] = {arg0, arg1, 0};
    // variables defined on stack... forced to load based on PC relative 

    USER_PROGRAM1("/sh", "/sh", "" /* does not care*/);    

    char console[] = {"console"};     
    if (call_sys_open(console, O_RDWR) < 0) {
        call_sys_mknod("console", CONSOLE, 0); /* STUDENT_TODO: replace this */
        call_sys_open(console, O_RDWR);
    }
    call_sys_dup(0); // stdout
    call_sys_dup(0); // stderr

    char msg[] = {"User process entry\n\r"};
    print_to_console(msg);

     
    /* STUDENT_TODO: your code here */
    call_sys_exec(path, argv);
}

void user_process1() {
    // project idea: complete mknod syscall? ask what files exist? 
    if (call_sys_open("console", O_RDWR) < 0) {
        call_sys_mknod("console", CONSOLE, 0); 
        call_sys_open("console", O_RDWR);
    }
    call_sys_dup(0); // stdout
    call_sys_dup(0); // stderr

    print_to_console("User process entry\n\r");

    int pid = call_sys_fork();
    if (pid < 0) {
        print_to_console("Error during fork\n\r");
        call_sys_exit(1);
        return;
    }
    print_to_console("fork() succeeds\n\r");

    static char *argv[] = {"arg0", "arg1", 0};

    if (pid == 0) {
        // loop1("abcde");
        call_sys_exec("/echo", argv);
    } else {
        loop1("12345");
    }
}

