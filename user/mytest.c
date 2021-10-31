#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define NFORKS 10

int main(int argc, char *argv[]){
    for(int i=0; i<NFORKS; i++)
    {
        int pid = fork();
        if(pid == 0)
        {
            while(1){
                
            }
        }
        else
            continue;
    }
    exit(0);
}