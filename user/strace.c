
#include "kernel/param.h"
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
    if (argc < 3 || to_int(argv[1]) == -1)
        fprintf(2, "Second argument NaN, or invalid number of arguments!!");

    if (trace(to_int(argv[1])) < 0)
    {
        fprintf(2, "%s: Invalid strace!!\n", argv[0]);
        exit(1);
    }
    exec(argv[2], argv + 2);
    exit(0);
}