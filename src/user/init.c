#include "src/kernel/type.h"
#include "src/kernel/stat.h"
#include "src/kernel/fcntl.h"
#include "src/user/users.h"


int main(){
    if(open("console",O_RDWR) < 0){

    }
    printf("-------\n");
    dup(0);
    dup(0);
    for(;;){
    }
    return 0;
}