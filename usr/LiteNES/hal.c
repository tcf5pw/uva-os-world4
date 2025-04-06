/*
Principle: minimum OS requirement. NO dependency on:
    -- thread (CLONE_VM, spinlock, semaphore, etc)
    -- non-blocking IO (O_NONBLOCK)
    -- SDL_xxx (libminisdl) functions (hence directly writes to /dev/fb)

Event dispatch is implemented as multiple tasks (processes) communicating with
pipelines.

Known issues: minor flickring & glitches at the canvas top (TBD)

---- Below: comment from NJU OS project ----
https://github.com/NJU-ProjectN/LiteNES/blob/master/src/hal.c

This file presents all abstractions needed to port LiteNES.
  (The current working implementation uses allegro library.)

To port this project, replace the following functions by your own:
1) nes_hal_init()
    Do essential initialization work, including starting a FPS HZ timer.

2) nes_set_bg_color(c)
    Set the back ground color to be the NES internal color code c.

3) nes_flush_buf(*buf)
    Flush the entire pixel buf's data to frame buffer.

4) nes_flip_display()
    Fill the screen with previously set background color, and
    display all contents in the frame buffer.

5) wait_for_frame()
    Implement it to make the following code is executed FPS times a second:
        while (1) {
            wait_for_frame();
            do_something();
        }

6) int nes_key_state(int b)
    Query button b's state (1 to be pressed, otherwise 0).
    The correspondence of b and the buttons:
      k - A
      j - B
      u - SELECT
      i - START
      w - UP
      s - DOWN
      a - LEFT
      d - RIGHT
*/
#include "hal.h"
#include "fce.h"
#include "common.h"
#include "../user.h"

// #define FB_FLIP  // use double framebuffer (fb) buffering?

static int cur_id = 0; // id of fb being rendered to, 0 or 1 (if FB_FLIP==1)

static char *vtx = 0;  // byte-to-byte mirror of hw fb (inc row padding - pitch)
static int vtx_sz = 0; // in bytes

static int dispinfo[MAX_DISP_ARGS] = {0}; // display config, loaded from procfs
static int fb = 0;                        // /dev/fb
static int fds[2];                        // pipes for exchanging events

// input events
struct event
{
    int type;
#define EV_INVALID INVALID // consistent with read_kb_event (user.h)
#define EV_KEYDOWN KEYDOWN // ditto
#define EV_KEYUP KEYUP     // ditto
#define EV_TIMER (KEYUP + 10)
    int scancode; // for EV_KEY
};

// keyboard: key state
char key_states[NUM_SCANCODES] = {EV_KEYUP}; // init: all EV_KEYUP

/* Wait until next timer event is fired. */
// quest: mario with inputs
void wait_for_frame()
{
    struct event ev;

#ifdef NES_PROFILE      // cf Makefile
    static int fps = 0; // measured fps
    static int total = 0, nframes = 0, last = 0;
    int t0 = uptime(); // ms

    if (last != 0)
        total += (t0 - last);
    nframes++;
    last = t0;

    if (total > 3000)
    { // print every ~3sec
        fps = 1000 / (total / nframes);
        printf("%s: FPS %d\n", __func__, fps);
        nframes = total = 0;
    }
#endif

    if (fds[0] <= 0)
    {
        printf("fatal:pipe invalid\n");
        exit(1);
    };

    while (1)
    {
        // read from pipe, from which we may get timer/kb events
        if (read(fds[0], &ev, sizeof ev) != sizeof ev)
        {
            printf("read ev failed");
            exit(1);
        }
        switch (ev.type)
        {
        case EV_TIMER:
            return;
        case EV_KEYDOWN:
        case EV_KEYUP:
            assert(ev.scancode < NUM_SCANCODES);
            // printf("key code %d %s\n", ev.scancode, ev.type == EV_KEYDOWN ? "down":"up");
            /* STUDENT_TODO: your code here */
            key_states[ev.scancode] = ev.type;
            break; // continue to wait for timer ev
        default:
            printf("unknown ev");
            exit(1);
            break;
        }
    }
    /* Project idea: support graceful exit. For a special key press (e.g. 'q'),
    returns from this func and goes back to fce_run which further exits the
    program but before that, need to notify the timer task & event task to quit
    too; the main task shall wait() them to quit.    */
}

// id: fb 0 or 1
static inline void setpixel(int id, char *buf, int x, int y, int pit, PIXEL p)
{
    assert(x >= 0 && y >= 0);
    y += (id * SCREEN_HEIGHT);
    *(PIXEL *)(buf + y * pit + x * PIXELSIZE) = p;
}

static inline PIXEL getpixel(int id, char *buf, int x, int y, int pit)
{
    assert(x >= 0 && y >= 0);
    y += (id * SCREEN_HEIGHT);
    return *(PIXEL *)(buf + y * pit + x * PIXELSIZE);
}

#define fcecolor_to_pixel(color) \
    (((char)color.r << 16) | ((char)color.g << 8) | ((char)color.b))

/* Set background color to vtx. RGB value of c is defined in fce.h */
void nes_set_bg_color(int c)
{
    int pitch = dispinfo[PITCH];
    PIXEL p = fcecolor_to_pixel(palette[c]);
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++)
            setpixel(cur_id, vtx, x, y, pitch, p);
}

/* Flush the pixel buffer. Materialize nes's drawing (unordered pixels) to fb.
         this lays out pixels in memory (in hw format)
   ver1: draw to an app fb, which is to be write to /dev/fb in one shot
   ver2: draw to a "back" fb (which will be made visible later)

    fb basics
    https://github.com/fxlin/p1-kernel/blob/master/docs/exp3/fb.md */
void nes_flush_buf(PixelBuf *buf)
{
    int pitch = dispinfo[PITCH]; // in bytes

    for (int i = 0; i < buf->size; i++)
    {
        Pixel *p = &buf->buf[i];
        int x = p->x, y = p->y;
        pal color = palette[p->c];
        PIXEL c = fcecolor_to_pixel(color);

        // Pixel could have coorindates x<0 (looks like fce shifts drawn
        //  pixels by applying offsets to them). These pixels shall be
        //  invisible on fb.
        assert(x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT);
        if (x >= 0)
        {
            setpixel(cur_id, vtx, x, y, pitch, c); // 1x scaling
            // Below: original code scales fce's raw pixels by 2x for larger display;
            //   since Rpi3 GPU will scale fb to display anyway, we don't need this
            // Keep it here for reference.
            // setpixel(vtx, x*2,      y*2,    pitch, c);
            // setpixel(vtx, x*2+1,    y*2,    pitch, c);
            // setpixel(vtx, x*2,      y*2+1,  pitch, c);
            // setpixel(vtx, x*2+1,    y*2+1,  pitch, c);
        }
    }
}

#define LINESIZE 128 // size of linebuf for reading /procfs
#define FPS_HZ 60

/* Initialization: (1) start a 1/FPS Hz timer. (2) create tasks that produce
    timer/kb events & dispatch them to the main task over pipes. cf usertests.c
    pipe1() pipe3()*/
// quest: kungfu, mario with inputs
void nes_hal_init()
{
    int n;
    struct event ev;

    if (pipe(fds) != 0)
    { // create pipe for passing events
        printf("%s: pipe() failed\n", __func__);
        exit(1);
    }

    // timer task, wakeup every frame and writes to pipe
    if (fork() == 0)
    {
        close(fds[0]);
        ev.type = EV_TIMER;
        printf("timer task running\n");
        while (1)
        {
            sleep(1000 / FPS_HZ); // in ms
            if (write(fds[1], &ev, sizeof ev) != sizeof ev)
            {
                printf("write timerev failed");
                exit(1);
            }
        }
        exit(0); // shall never reach here
    }

    // keyboard task, read kb events and writes to pipe
    // cf usertests.c pipe3()
    // quest: mario with inputs
    if (fork() == 0)
    {
        close(fds[0]);
        // open the keyboard device file
        int events = 0;
        assert(events >= 0);
        int evtype;
        unsigned int scancode;
        printf("input task running\n");

        while (1)
        {
            n = read_kb_event(events, &evtype, &scancode);
            if (n || scancode == 0 || scancode >= NUM_SCANCODES || evtype == EV_INVALID)
            {
                printf("read_kb_event failed\n");
            }
            else
            { // pass the kb event to main task
                // printf("evtype %d scancode %d\n", evtype, (int)scancode);

                /* STUDENT_TODO: your code here */
                ev.type = evtype;
                ev.scancode = scancode;
                write(fds[1], &ev, sizeof ev);
            }
        }
        exit(0); // shall never reach here
    }
    close(fds[1]);

    // open the framebuffer device
    fb = open("/dev/fb", O_RDWR); /* STUDENT_TODO: replace this */
    assert(fb > 0);

    // Configure fb hardware via procfs
    // ask for a vframebuf that scales the native fce canvas by 2x
    n = config_fbctl(SCREEN_WIDTH, SCREEN_HEIGHT, // desired viewport ("phys")
#ifdef FB_FLIP
                     SCREEN_WIDTH, 2 * SCREEN_HEIGHT, 0, 0); // two fbs, each contig (can be write to /dev/fb in a shot
#else
                     SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0);
#endif
    assert(n == 0);

    read_dispinfo(dispinfo, &n);
    assert(n > 0);
    printf("SCREEN_WIDTH=%d SCREEN_HEIGHT=%d\n ",
           SCREEN_WIDTH, SCREEN_HEIGHT);
    printf("/proc/dispinfo: width %d height %d vwidth %d vheight %d"
           "pitch %d depth %d isrgb %d\n",
           dispinfo[WIDTH], dispinfo[HEIGHT], dispinfo[VWIDTH], dispinfo[VHEIGHT],
           dispinfo[PITCH], dispinfo[DEPTH], dispinfo[ISRGB]);

    // Calc vtx size and alloc buf. NB: pitch is in bytes
    vtx_sz = dispinfo[PITCH] * dispinfo[VHEIGHT];
    vtx = malloc(vtx_sz);
    if (!vtx)
    {
        printf("failed to alloc vtx\n");
        exit(1);
    }
    printf("fb alloc ...ok\n");

    // XXX: free vtx in graceful exit (if implemented
}

/* Update screen at FPS rate by drawing function.
   Timer ensures this function is called FPS times a second. */
// quest: kungfu
void nes_flip_display()
{
    int n;
    assert(fb && vtx && vtx_sz);

#ifdef FB_FLIP
    int sz = SCREEN_HEIGHT * dispinfo[PITCH];
    n = lseek(fb, cur_id * sz, SEEK_SET);
    assert(n == cur_id * sz);
    if ((n = write(fb, vtx + cur_id * sz, sz)) != sz)
    { // a bug??
        printf("%s: failed to write to hw fb. fb %d sz %d ret %d\n",
               __func__, fb, sz, n);
    }
    // make the current fb visible on display
    n = config_fbctl(0, 0, 0, 0 /*dc*/, 0 /*xoff*/, cur_id * SCREEN_HEIGHT /*yoff*/);
    assert(n == 0);
    cur_id = 1 - cur_id;
#else

    /* STUDENT_TODO: your code here */
    lseek(fb, 0, SEEK_SET);
    n = write(fb, vtx, vtx_sz);
    if (n != vtx_sz)
    {
        printf("%s: failed to write to hw fb. fb %d sz %d ret %d\n", __func__, fb, vtx_sz, n);
    }
#endif
}

/* Query a button's state. b: the button idx.
   Returns 1 if button #b is pressed. */
int nes_key_state(int b)
{
    switch (b)
    {
    case 0: // On / Off
        return 1;
    case 1:                                   // A  (k)
        return key_states[KEY_K] == EV_KEYDOWN; /* STUDENT_TODO: replace this */
    case 2:                                   // B  (j)
        return key_states[KEY_J] == EV_KEYDOWN; /* STUDENT_TODO: replace this */
    case 3:                                   // SELECT (u)
        return key_states[KEY_U] == EV_KEYDOWN; /* STUDENT_TODO: replace this */
    case 4:                                   // START  (i)
        return key_states[KEY_I] == EV_KEYDOWN; /* STUDENT_TODO: replace this */
    case 5:                                   // UP  (w)
        return key_states[KEY_W] == EV_KEYDOWN; /* STUDENT_TODO: replace this */
    case 6:                                   // DOWN (s)
        return key_states[KEY_S] == EV_KEYDOWN; /* STUDENT_TODO: replace this */
    case 7:                                   // LEFT (a)
        return key_states[KEY_A] == EV_KEYDOWN; /* STUDENT_TODO: replace this */
    case 8:                                   // RIGHT (d)
        return key_states[KEY_D] == EV_KEYDOWN; /* STUDENT_TODO: replace this */
    default:
        return 0;
    }
}
