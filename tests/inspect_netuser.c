/* A deliberately "bad" program for the thc-inspect imports check: it imports socket(). Never run. */
#ifndef _WIN32
#include <sys/socket.h>
int main(int argc, char **argv)
{
    (void)argv;
    return argc > 99 ? socket(AF_INET, SOCK_STREAM, 0) : 0;   /* never true */
}
#else
int main(void) { return 0; }
#endif
