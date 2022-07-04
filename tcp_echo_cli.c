#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include<sys/types.h>
#include<sys/wait.h>
#define MAX_CMD_STR 100

#define myprintf(fp, prtstr, ...)           \
    if (fp == NULL)                         \
    {                                       \
        printf(prtstr, ##__VA_ARGS__);      \
    }                                       \
    else                                    \
    {                                       \
        printf(prtstr, ##__VA_ARGS__);      \
        fprintf(fp, prtstr, ##__VA_ARGS__); \
        fflush(fp);                         \
    }


int sig_type = 0;
FILE * fp_res = NULL;
void* Mymemcpy(void *dest,const void* src,size_t count);
void *pmemset(void *s , char ch , int n);
int mystrncmp(const char *s1, const char *s2, size_t n);

void sig_pipe(int signo) {
    sig_type = signo;
    pid_t pid = getpid();
    myprintf(fp_res, "[cli](%d) SIGPIPE is coming!\n", pid);
}

void sig_chld(int signo) {
    sig_type = signo;
    pid_t pid = getpid(), pid_chld = 0;
    int stat;
    myprintf(fp_res, "[cli](%d) SIGCHLD is coming!\n", pid);
    while ((pid_chld = waitpid(-1, &stat, WNOHANG)) > 0){
        myprintf(fp_res, "[cli](%d) child process(%d) terminated.\n", pid, pid_chld);
    }
}


int echo_rqt(int sockfd, int pin)
{
    pid_t pid = getpid();
    //PDU定义
    //len_h：表示主机序的PDU数据长度，len_n：表示网络序的PDU数据长度
    int len_h = 0, len_n = 0;
    //pin_h：表示主机序的进程序号，pin_n：表示网络序的进程序号
    int pin_h = pin, pin_n = htonl(pin);
    
    char file[10] = {'\0'};
    char buf[MAX_CMD_STR+1+8] = {0};

    //这里的file表示测试文件名
    sprintf(file, "td%d.txt", pin);

    FILE * fp_td = fopen(file, "r");
    if(!fp_td){
        myprintf(fp_res, "[cli](%d) Test data read error!\n", pin_h);
        return 0;//echo_rqt函数退出
    }

    //从测试文件读取一行测试数据，读取一行字符串
    while (fgets(&buf[8], MAX_CMD_STR, fp_td)) {

        pin_h = pin;
        pin_n = htonl(pin);

        // 指令解析:
        // 收到指令"exit"，跳出循环并返回
        if(mystrncmp(&buf[8], "exit", 4) == 0){
            break;
        }

        Mymemcpy(buf, &pin_n, 4);
        // 获取数据长度
        len_h = strnlen(&buf[8], MAX_CMD_STR);
        // 将数据长度写入PDU缓存（网络字节序）
        len_n = htonl(len_h);
        Mymemcpy(&buf[4], &len_n, 4);

        // 将读入的'\n'更换为'\0'
        if(buf[len_h+8-1] == '\n')
            buf[len_h+8-1] = 0; // 同'\0'

        // 发送数据:
        write(sockfd, buf, len_h+8);

        // 读取echo_rep数据:
        pmemset(buf, 0, sizeof(buf));
        // 读取PIN（网络字节序）
        read(sockfd, &pin_n, 4);
        // 读取服务器echo_rep数据长度（网络字节序）并转为主机字节序
        read(sockfd, &len_n, 4);
        len_h = ntohl(len_n);

        // 读取服务器echo_rep数据
        read(sockfd, buf, len_h);
        myprintf(fp_res,"[echo_rep](%d) %s\n", pid, buf);
    }
    return 0;
}


int main(int argc, char* argv[])
{
    // 基于argc简单判断命令行指令输入是否正确
    if(argc != 4){
        printf("Usage:%s <IP> <PORT> <CONCURRENT AMOUNT>\n", argv[0]);
        return 0;//主函数的返回值为0
    }

    //定义SIGPIPE信号的相关信息
    struct sigaction sigact_pipe, old_sigact_pipe;
    sigact_pipe.sa_handler = sig_pipe;
    sigemptyset(&sigact_pipe.sa_mask);
    sigact_pipe.sa_flags = 0;
    sigact_pipe.sa_flags |= SA_RESTART;
    sigaction(SIGPIPE, &sigact_pipe, &old_sigact_pipe);

    //定义SIGCHLD信号的相关信息
    struct sigaction sigact_chld, old_sigact_chld;
    sigact_chld.sa_handler = &sig_chld;
    sigemptyset(&sigact_chld.sa_mask);
    sigact_pipe.sa_flags = 0;
    sigact_pipe.sa_flags |= SA_RESTART;
    sigaction(SIGCHLD, &sigact_chld, &old_sigact_chld);

    struct sockaddr_in srv_addr;

    int connfd;					
    int conc_amnt = atoi(argv[3]);

    // 获取当前（父）进程PID
    pid_t pid = getpid();

    //初始化服务器端的地址结构信息
    pmemset(&srv_addr, 0, sizeof(srv_addr));//地址结构首先清0
    srv_addr.sin_family = AF_INET;//设置family为IPv4地址族
    inet_pton(AF_INET, argv[1], &srv_addr.sin_addr);
    srv_addr.sin_port = htons(atoi(argv[2]));

    //for循环创建规定的并发进程，通过fork()函数，父子进程的区别是根据fork的返回值
    //返回0表示是子进程,返回非0值表示是父进程（子进程的进程号）
    for (int i = 0; i < conc_amnt - 1; i++) {
        if (!fork()) {	//子进程
            //每增加一个子进程，pin号加1
            int pin = i+1;
            char fn_res[20];
            // 获取当前子进程PID,用于后续子进程信息打印
            pid = getpid();//用于获得子进程的进程号PID

            // 打开客户端记录res文件，文件序号指定为当前子进程序号PIN；
            sprintf(fn_res, "stu_cli_res_%d.txt", pin);//拼接文件名：stu_cli_res_pin.txt

            //文件顺利打开后，指向该流的文件指针就会被返回
            fp_res = fopen(fn_res, "ab"); 
            //有个保护判断，如果文件打开失败，则返回NULL，并把错误代码存在errno中
            if(!fp_res){
                //输出文件打开失败的提示信息
                printf("[cli](%d) child exits, failed to open file \"stu_cli_res_%d.txt\"!\n", pid, pin);
                exit(-1);//文件打开失败就直接退出子进程
            }

            //向文件中写入子进程创建的信息，具体格式[cli](进程号) child process 进行序号 is created!
            myprintf(fp_res, "[cli](%d) child process %d is created!\n", pid, pin);

            //创建客户端的套接字描述符
            connfd = socket(PF_INET, SOCK_STREAM, 0);

            do{	
                
                int res = connect(connfd, (struct sockaddr*) &srv_addr, sizeof(srv_addr));
                if(!res){
                    char ip_str[20]={0};

                    
                    myprintf(fp_res, "[cli](%d) server[%s:%d] is connected!\n", pid, \
						inet_ntop(AF_INET, &srv_addr.sin_addr, ip_str, sizeof(ip_str)), \
							ntohs(srv_addr.sin_port));
                    
                    if(!echo_rqt(connfd, pin))//echo_rqt()正常返回为0
                        break;//终止循环，执行后面关闭套接字描述符
                }
                else	//TCP连接建立失败
                    break;	//终止循环，执行后面关闭套接字描述符
            }while(1);

            //关闭套接字描述符
            close(connfd);
            //注意一定要包含向文件中写入的相关信息
            myprintf(fp_res, "[cli](%d) connfd is closed!\n", pid);
            myprintf(fp_res, "[cli](%d) child process is going to exit!\n", pid);

            // 关闭子进程打开的res文件
            if(fp_res){
                if(!fclose(fp_res))
                    printf("[cli](%d) stu_cli_res_%d.txt is closed!\n", pid, pin);
            }
            exit(1);//子进程直接退出
        }
        else{
            //父进程
            continue;//继续执行，直接空行也可以
        }
    }

    //父进程要创建套接字
    char fn_res[20];
    //父进程对应的记录文件名，stu_cli_res_0.txt
    sprintf(fn_res, "stu_cli_res_%d.txt", 0);
    fp_res = fopen(fn_res, "wb");//打开父进程对应的记录文件
    if(!fp_res){
        printf("[cli](%d) child exits, failed to open file \"stu_cli_res_0.txt\"!\n", pid);
        exit(-1);
    }

    //父进程中，也需要向服务器端发送数据，因此也需要建立TCP连接
    connfd = socket(PF_INET, SOCK_STREAM, 0);
    do{
        //发起TCP连接请求
        int res = connect(connfd, (struct sockaddr*) &srv_addr, sizeof(srv_addr));
        //
        if(!res){
            char ip_str[20]={0};
            myprintf(fp_res, "[cli](%d) server[%s:%d] is connected!\n", pid, inet_ntop(AF_INET, &srv_addr.sin_addr, ip_str, sizeof(ip_str)), ntohs(srv_addr.sin_port));
            if(!echo_rqt(connfd, 0))//
                break;//成功完成字符串的发送和返回输出，终止循环，执行后面关闭套接字描述符
        }
        else
            break;
    }while(1);

    // 关闭连接描述符
    close(connfd);
    myprintf(fp_res, "[cli](%d) connfd is closed!\n", pid);
    myprintf(fp_res, "[cli](%d) parent process is going to exit!\n", pid);

    if(!fclose(fp_res))
        printf("[cli](%d) stu_cli_res_0.txt is closed!\n", pid);

    return 0;
}

void* Mymemcpy(void *dest,const void* src,size_t count) {
    char *tmpDest = (char *)dest;
    char *tmpSrc = (char *)src;

    size_t i;
    //内存有覆盖的区域，从尾部开始复制
    if((tmpDest > tmpSrc) && (tmpDest < (tmpSrc+count))) {
        for(i = count-1; i != -1; i--) {
            tmpDest[i] = tmpSrc[i];
        }
    }
    else {
        //内存没有覆盖的区域，从头开始复制
        for(i = 0; i < count; i++) {
            tmpDest[i] = tmpSrc[i];
        }
    }
    return dest;
}


void *pmemset(void *s , char ch , int n) {
    char *temp_s=(char *)s;
    while (n--)
    {
        *temp_s=ch;
        temp_s++;
    }

    return s;
}

int mystrncmp(const char *s1, const char *s2, size_t n) {
    int i = 0;
    while(i < n) {
        if(s1[i] > s2[i]) {
            return 1;
        }
        else if(s1[i] < s2[i]) {
            return -1;
        }

        i ++;
    }

    return 0;
}

