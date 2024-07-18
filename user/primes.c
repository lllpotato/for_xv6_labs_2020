#include "kernel/types.h"
#include "user/user.h"

void mapping(int n,int pd[]){
	close(n);
	dup(pd[n]);
	close(pd[0]);
	close(pd[1]);

}

void primes(){
	int prev,next;
	int fd[2];
	if(read(0,&prev,sizeof(int))){
		printf("prime %d\n",prev);
		pipe(fd);
		if(fork()==0){
			mapping(1,fd);
			while(read(0,&next,sizeof(int))){
				if(next%prev!=0){
					write(1,&next,sizeof(int));
				}
			}
		}else{
			wait(0);
			mapping(0,fd);
			primes();
		}
	}

}

int main(int argc,char* argv[]){
	int fd[2];
	pipe(fd);
	if(fork()==0){
		mapping(1,fd);
		for(int i = 2;i<=35;i++){
			write(1,&i,sizeof(int));
		}
	}else {
		wait(0);
		mapping(0,fd);
		primes();
	}
	exit(0);

}
