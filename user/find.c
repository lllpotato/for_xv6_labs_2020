#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void
find(char *dir,char *file)
{
  char buf[512], *p;
  int fd;
  struct dirent de;//目录表
  struct stat st;//文件状态信息：包括具体的文件或者目录文件

  if((fd = open(dir, 0)) < 0){//打开文件，获取文件描述符
    fprintf(2, "find: cannot open %s\n", dir);
    return;
  }

  if(fstat(fd, &st) < 0){//将文件描述符对应的文件信息存入 st这个文件状态信息结构体中
    fprintf(2, "find: cannot stat %s\n", dir);
    close(fd);
    return;
  }

  switch(st.type){//查看读取到的文件是 文件(file)还是目录(dir)
  case T_FILE:
    fprintf(2, "find: %s is not a dir!\n", dir);//不是文件类型，直接报错
    break;

  case T_DIR:
    if(strlen(dir) + 1 + DIRSIZ + 1 > sizeof buf){//如果是目录，则计算一下该目录的长度，加上规定的文件名字的上限长度，有没有超过buf的容量，超过了就报错
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, dir);//没有超过，将目录复制进buf
    p = buf+strlen(buf);//用一个指针p指向buf的最后面的位置
    *p++ = '/';//在后面加上“/”斜杠，为后面添加文件名做准备
    while(read(fd, &de, sizeof(de)) == sizeof(de)){//读取这个目录的目录表信息，将n 个字节读入de；返回读取的字节数；如果文件结束，返回0
      if(de.inum == 0)//0可能表示是个空目录 跳过 (1.每个文件和目录都有一个唯一的 inode 号，文件系统通过 inode 号来区分不同的文件和目录。2.Inode 号指向一个 inode 结构体，该结构体包含了文件或目录的元数据（如大小、所有者、权限、时间戳等）)
        continue;

      if(!strcmp(de.name, ".") || !strcmp(de.name, ".."))
        continue;

      memmove(p, de.name, DIRSIZ);//读取到了信息，不是空目录，把这个目录下的文件名拷贝到之前的p指针，形成一个完整的文件路径
      p[DIRSIZ] = 0;//加上休止符？  等同于*(p+DIRSIZ) = 0??
      if(stat(buf, &st) < 0){//将指定名称的文件信息放入*st,如果打开失败，报错
        printf("find: cannot stat %s\n", buf);
        continue;
      }

      if(st.type==T_DIR){
        find(buf,file);
      }else if(st.type==T_FILE&&!strcmp(de.name,file)) {
        printf("%s\n", buf);//打开成功，打印文件信息
      }
    }
    break;
  }

  close(fd);
}

int
main(int argc, char *argv[])
{

  if(argc != 3){//参数不为3
    fprintf(2,"usage: find dirName fileName\n");
    exit(0);
  }
  find(argv[1],argv[2]);
  exit(0);
}
