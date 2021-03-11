#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"


struct fortest {
  int x[1024];        
};


int main(int argc, char** argv){

    struct fortest *test = malloc(4096);
    sbrk(20*4096);
    test->x[0] = 5;
    printf(1, "%d\n", test->x[0]);
    exit();
}