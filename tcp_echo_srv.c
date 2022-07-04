#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <assert.h>

#define MAXLISTEN 1024
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

int sig_type = 0, sig_to_exit = 0;
FILE * fp_res = NULL;

void sig_int(int signo) {
    sig_type = signo;
    pid_t pid = getpid();
    myprintf(fp_res, "[srv](%d) SIGINT is coming!\n", pid);
    sig_to_exit = 1;
}
void sig_pipe(int signo) {
    sig_type = signo;
    pid_t pid = getpid();
    myprintf(fp_res, "[srv](%d) SIGPIPE is coming!\n", pid);
}
void sig_chld(int signo) {
    sig_type = signo;
    pid_t pid = getpid(), pid_chld = 0;
    int stat;
    myprintf(fp_res, "[srv](%d) SIGCHLD is coming!\n", pid);
    while ((pid_chld = waitpid(-1, &stat, WNOHANG)) > 0){
    }
}
int install_sig_handlers(){
    int res = -1;
    struct sigaction sigact_pipe, old_sigact_pipe;
    sigact_pipe.sa_handler = sig_pipe;//sig_pipe()，信号处理函数
    sigact_pipe.sa_flags = 0;
    sigact_pipe.sa_flags |= SA_RESTART;
    sigemptyset(&sigact_pipe.sa_mask);
    res = sigaction(SIGPIPE, &sigact_pipe, &old_sigact_pipe);
    if(res)
        return -1;

    struct sigaction sigact_chld, old_sigact_chld;
    
    sigact_chld.sa_handler = sig_chld;
    sigact_chld.sa_flags = 0;
    sigact_chld.sa_flags |= SA_RESTART;//设置受影响的慢系统调用重启
    sigemptyset(&sigact_chld.sa_mask);
    res = sigaction(SIGCHLD, &sigact_chld, &old_sigact_chld);

    if(res)
        return -2;

    struct sigaction sigact_int, old_sigact_int;
    sigemptyset(&sigact_int.sa_mask);
    sigact_int.sa_flags = 0;
    sigact_int.sa_handler = &sig_int;
    sigaction(SIGINT, &sigact_int, &old_sigact_int);
    
    if(res)
        return -3;
    
    return 0;
}


int echo_rep(int sockfd)
{
    //初始时，len_h（主机序的PDU的长度），pin_h（主机序的客户端子进程创建序号）
    int len_h = -1, len_n = -1;
    int pin_h = -1, pin_n = -1;
    int res = 0;
    char *buf = NULL;
    //当前的进程号
    pid_t pid = getpid();

    // 读取客户端PDU并执行echo回复
    do {
        
        do {
            
            res = read(sockfd, &pin_n, sizeof(pin_n));
            if(res < 0){
                myprintf(fp_res, "[srv](%d) read pin_n return %d and errno is %d!\n", pid, res, errno);
                if(errno == EINTR){
                    if(sig_type == SIGINT)
                        return pin_h;
                    continue;
                }
                return pin_h;
            }
            if(!res){
                return pin_h;
            }
            
            pin_h = ntohl(pin_n);
            break;//跳出
        }while(1);

        // 读取客户端echo_rqt数据长度
        do{
            //用read读取客户端echo_rqt数据长度（网络字节序）到len_n中:返回值赋给res
            res = read(sockfd, &len_n, sizeof(len_n));

            if(res < 0){
                myprintf(fp_res, "[srv](%d) read len_n return %d and errno is %d\n", pid, res, errno);
                if(errno == EINTR){
                    if(sig_type == SIGINT)
                        return len_h;
                    continue;
                }
                return len_h;
            }
            if(!res){
                return len_h;
            }
            //将len_n字节序转换后存放到len_h中
            len_h = ntohl(len_n);
            break;
        }while(1);

        // 读取客户端echo_rqt数据
        //初始时，len_to_read就是收到的PDU的LEN值
        int read_amnt = 0, len_to_read = len_h;
        buf = (char*)malloc(len_h * sizeof(char)+8); // 预留PID与数据长度的存储空间，为后续回传做准备
        do{
           
            res = read(sockfd, &buf[read_amnt]+8, len_to_read);

            if(res < 0){
                myprintf(fp_res, "[srv](%d) read data return %d and errno is %d,\n", pid, res, errno);
                if(errno == EINTR){
                    if(sig_type == SIGINT){
                        free(buf);
                        return pin_h;
                    }
                    continue;
                }
                free(buf);
                return pin_h;
            }
            if(!res){
                free(buf);
                return pin_h;
            }

            read_amnt += res;
            if(read_amnt == len_h) {
                break;
            }
            else if(read_amnt < len_h) {
                len_to_read = len_h - read_amnt;
            }
            else {
                free(buf);
                return pin_h;
            }
        }while(1);

     
        myprintf(fp_res, "[echo_rqt](%d) %s\n", pid, buf+8);
        
        memcpy(buf, &pin_n, 4);
        
        memcpy(buf+4, &len_n, 4);

        // 发送echo_rep数据:
        write(sockfd, buf, len_h+8);
        free(buf);
    }while(1);
    return pin_h;
}


int main(int argc, char* argv[])
{
    if(argc != 3)
    {
        //输出正确格式的提示信息
        printf("Usage:%s <IP> <PORT>\n", argv[0]);
        return -1;
    }

    pid_t pid = getpid();
    // 定义IP地址字符串（点分十进制）缓存，用于后续IP地址转换；
    //用于IP地址转换，初始化清0
    char ip_str[20];
    char fn_res[20];
    for (int i = 0; i < 20; i++) {
        ip_str[i] = 0;
        fn_res[i] = 0;
    }
    int res = -1;

    // 安装信号处理器，包括SIGPIPE，SIGCHLD以及SIGITN；
    //install_sig_handlers函数正常退出，返回0，异常退出分别返回-1，-2，-3
    res = install_sig_handlers();

    //如果信号安装有问题，则打印错误信息，并退出主函数
    if(res){
        printf("[srv](%d) parent exit failed to install signal handlers!\n", pid);
        return res;
    }

    // 打开文件"stu_srv_res_p.txt"，用于后续父进程信息记录；
    fp_res = fopen("stu_srv_res_p.txt", "wb");
    if(!fp_res){
        printf("[srv](%d) failed to open file \"stu_srv_res_p.txt\"!\n", pid);
        return res;
    }
    //将文件被打开的提示信息打印到stdout
    printf("[srv](%d) stu_srv_res_p.txt is opened!\n", pid);

    // 定义服务器Socket地址srv_addr，以及客户端Socket地址cli_addr；
    struct sockaddr_in srv_addr, cli_addr;
    socklen_t cli_addr_len;
    int listenfd, connfd;

    // 初始化服务器Socket地址srv_addr，其中会用到argv[1]、argv[2]
    bzero(&srv_addr, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    srv_addr.sin_port = htons(atoi(argv[2]));

    inet_ntop(AF_INET, &srv_addr.sin_addr, ip_str, sizeof(ip_str));
   
    myprintf(fp_res, "[srv](%d) server[%s:%d] is initializing!\n", pid, ip_str, (int)ntohs(srv_addr.sin_port));
    // 获取Socket监听描述符: listenfd = socket(x,x,x);
    listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if(listenfd == -1)
        return listenfd;

    // 绑定服务器Socket地址: res = bind(x,x,x);成功返回0，失败返回-1
    res = bind(listenfd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));

    //bind失败，退出主程序
    if(res)
        return res;
    res = -9;
    res = listen(listenfd, MAXLISTEN);

    if(res)//res为非0
        printf("[srv](%d) listen() returned %d\n", pid, res);
    else if(res == 0)//listen命令设置被动监听模式成功
        printf("[srv](%d) listen() returned 0\n",pid);

    while(!sig_to_exit)
    {
        // 获取cli_addr长度，执行accept()：connfd = accept(x,x,x);
        cli_addr_len = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &cli_addr_len);
        
        if(connfd == -1 && errno == EINTR){
            if(sig_type == SIGINT)
                break;
            continue;
        }
        
        inet_ntop(AF_INET, &cli_addr.sin_addr, ip_str, sizeof(ip_str));
        myprintf(fp_res, "[srv](%d) client[%s:%d] is accepted!\n", pid, ip_str, (int)ntohs(cli_addr.sin_port));
        
        fflush(fp_res);

        if(!fork()){//子进程
            
            pid = getpid();
           
            sprintf(fn_res, "stu_srv_res_%d.txt", pid);
            fp_res = fopen(fn_res, "wb");
            if(!fp_res){
                printf("[srv](%d) child exits, failed to open file \"stu_srv_res_%d.txt\"!\n", pid, pid);
                exit(-1);
            }
            //文件打开成功，向记录文件中写入[srv](进程号) child process is created!
            myprintf(fp_res, "[srv](%d) child process is created!\n", pid);

            //子进程中，只是为客户端传输数据，因此关闭监听描述符
            //关闭监听描述符，打印提示信息到文件中
            close(listenfd);
            myprintf(fp_res, "[srv](%d) listenfd is closed!\n", pid);

            //执行业务函数echo_rep（返回客户端PIN到变量pin中，以便用于后面的更名操作）
            int pin = echo_rep(connfd);
            if(pin < 0) {
                myprintf(fp_res, "[srv](%d) child exits, client PIN returned by echo_rqt() error!\n", pid);
                exit(-1);
            }

            //更名子进程res文件(PIN替换PID)
            char fn_res_n[20]= {0};
            //建立记录文件需要更名的新名字， stu_srv_res_客户端进程序号.txt
            sprintf(fn_res_n, "stu_srv_res_%d.txt", pin);
            if(!rename(fn_res, fn_res_n)){
                //更名成功，将日志写入文件
                myprintf(fp_res, "[srv](%d) res file rename done!\n", pid);
            }
            else {
                //更名失败
                myprintf(fp_res, "[srv](%d) child exits, res file rename failed!\n", pid);
            }

            //关闭连接描述符，输出信息到res文件中
            close(connfd);
            myprintf(fp_res, "[srv](%d) connfd is closed!\n", pid);
            myprintf(fp_res, "[srv](%d) child process is going to exit!\n", pid);

            if(!fclose(fp_res))
                printf("[srv](%d) stu_srv_res_%d.txt is closed!\n", pid, pin);

            //退出子进程
            exit(1);
        }
        else{// 父进程
            close(connfd);// 关闭服务套接字描述符，主进程只负责监听
            continue;// 继续accept()，处理下一个请求
        }
    }

    close(listenfd);
    myprintf(fp_res, "[srv](%d) listenfd is closed!\n", pid);
    myprintf(fp_res, "[srv](%d) parent process is going to exit!\n", pid);

    // 关闭父进程res文件
    if(!fclose(fp_res))
        printf("[srv](%d) stu_srv_res_p.txt is closed!\n", pid);
    return 0;
}
