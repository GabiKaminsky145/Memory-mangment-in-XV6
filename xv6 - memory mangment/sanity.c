#include "types.h"
#include "stat.h"
#include "user.h"


#define pgsize 4096

int main(){



  char * allocate = (char*)malloc(pgsize*20);
  int i;
  for (i = 0; i < 40; i++){
    allocate[i * pgsize/2] = 'i';
  }





















    printf(1, "allocate[%d] = %s\n",1 * pgsize/2, allocate + 1 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",2 * pgsize/2, allocate + 2 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",3 * pgsize/2, allocate + 3 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",4 * pgsize/2, allocate + 4 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",5 * pgsize/2, allocate + 5 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",6 * pgsize/2, allocate + 6 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",7 * pgsize/2, allocate + 7 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",8 * pgsize/2, allocate + 8 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",9 * pgsize/2, allocate + 9 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",10 * pgsize/2, allocate + 10 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",11 * pgsize/2, allocate + 11 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",12 * pgsize/2, allocate + 12 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",13 * pgsize/2, allocate + 13 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",14 * pgsize/2, allocate + 14 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",15 * pgsize/2, allocate + 15 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",16 * pgsize/2, allocate + 16 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",17 * pgsize/2, allocate + 17 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",18 * pgsize/2, allocate + 18 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",19 * pgsize/2, allocate + 19 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",20 * pgsize/2, allocate + 20 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",21 * pgsize/2, allocate + 21 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",22 * pgsize/2, allocate + 22 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",39 * pgsize/2, allocate + 39 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",38 * pgsize/2, allocate + 38 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",37 * pgsize/2, allocate + 37 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",36 * pgsize/2, allocate + 36 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",35 * pgsize/2, allocate + 35 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",34 * pgsize/2, allocate + 34 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",23 * pgsize/2, allocate + 23 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",33 * pgsize/2, allocate + 33 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",24 * pgsize/2, allocate + 24 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",30 * pgsize/2, allocate + 30 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",31 * pgsize/2, allocate + 31 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",32 * pgsize/2, allocate + 32 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",25 * pgsize/2, allocate + 25 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",27 * pgsize/2, allocate + 27 * pgsize/2);
    printf(1, "allocate[%d] = %s\n",26 * pgsize/2, allocate + 26 * pgsize/2);
  printf(1, "sanity malloc test OK\n");
  exit();
}


