#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int to_int(char *s)
{
    int i = 0;
    char *temp = s;
    while (*temp)
    {
        if (*temp >= '0' && *temp <= '9')
            i = i * 10 + *temp++ - '0';
        else
            return -1;
    }
    return i;
}

int main(int argc, char *argv[])
{

    if (argc != 3)
    {
        printf("Error: Invalid number of arguments.\n");
        exit(1);
    }
    int new_priority = to_int(argv[1]);
    int pid = to_int(argv[2]);
    if (new_priority < 0 || new_priority > 100)
    {
        printf("Error: Priority should be in range [0,100]\n");
        exit(1);
    }
    int old_priority = setpriority(new_priority, pid);
    if (old_priority == -1)
    {
        printf("Error: Process not found\n");
        exit(1);
    }
    else
    {
        printf("Process with PID: %d updated from priority %d to %d.\n", pid, old_priority, new_priority);
        exit(0);
    }
}