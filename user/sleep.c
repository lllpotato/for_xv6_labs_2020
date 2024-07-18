#include "kernel/types.h"
#include "user/user.h"

int main (int argc,char* argv[]){
	if(argc!=2){
		write(2,"Usage sleep:Argc err\n",strlen("Usage sleep:Argc err\n"));
		exit(1);
	}

	int time  = atoi(argv[1]);
	sleep(time);
	exit(0);

}
