#include <stdio.h>
#include <stdlib.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_SLOTS 10
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
typedef struct{
    char url[MAXLINE];
    char object[MAX_OBJECT_SIZE];
    size_t size;
    int valid;
    unsigned long last_used;
}cache_item;
cache_item cache[CACHE_SLOTS];
unsigned long use_clock=0;
pthread_rwlock_t cache_lock;
void cache_init(void){
    int i;
    for(i=0;i<CACHE_SLOTS;i++){
        cache[i].valid=0;
        cache[i].size=0;
        cache[i].last_used=0;
    }
    use_clock=0;
    pthread_rwlock_init(&cache_lock,NULL);
};
int cache_find(char*url,char*object,size_t *size){
    pthread_rwlock_rdlock(&cache_lock);
    for(int i=0;i<CACHE_SLOTS;i++){
        if(cache[i].valid==0){
            continue;
        }
        if(strcmp(cache[i].url,url)==0){
            memcpy(object,cache[i].object,cache[i].size);
            *size=cache[i].size;
            pthread_rwlock_unlock(&cache_lock);
            return 1;
        }
        
    }
    pthread_rwlock_unlock(&cache_lock);
    return 0;
}
void cache_insert(char* url,char*object,size_t size){
    pthread_rwlock_wrlock(&cache_lock);
    for(int i=0;i<CACHE_SLOTS;i++){
        if(cache[i].valid==0){
            strcpy(cache[i].url,url);
            memcpy(cache[i].object,object,size);
            cache[i].size=size;
            cache[i].valid=1;
            use_clock++;
            cache[i].last_used=use_clock;
            pthread_rwlock_unlock(&cache_lock);
            return;
        }
    }
    int temp=0;
    unsigned temp1=cache[0].last_used;
    for(int i=0;i<CACHE_SLOTS;i++){
        if(cache[i].last_used<temp1){
            temp1=cache[i].last_used;
            temp=i;
        }
    }
    strcpy(cache[temp].url,url);
    memcpy(cache[temp].object,object,size);
    cache[temp].size=size;
    use_clock++;
    cache[temp].last_used=use_clock;
    pthread_rwlock_unlock(&cache_lock);
    return;
}
void parse_url(char *url,char*hostname,char*port,char*path){
    char*start=&url[7];
    char*slash=strchr(start,'/');
    size_t hostlen;
    if(slash==NULL){
        strcpy(path,"/");
        hostlen=strlen(start);
    }    
    else{
        strcpy(path,slash);
        hostlen=slash-start;
    }
    strncpy(hostname,start,hostlen);
    hostname[hostlen]='\0';
    char*temp=strchr(hostname,':');
    if(temp==NULL){
        strcpy(port,"80");
    }
    else{
        size_t hostnamelen=temp-hostname;
        char*hoststart=hostname;
        strncpy(hostname,hoststart,hostnamelen);
        hostname[hostnamelen]='\0';
        strcpy(port,temp+1);
    }
}
void doit(int connfd){
        int severfd;
        rio_t rio;
        rio_t sever_rio;
        char sever_buf[MAXBUF];
        char buf[MAXBUF];
        ssize_t n;
        ssize_t sever_n;
        char method[MAXLINE];
        char url[MAXLINE];
        char version[MAXLINE];
        char host_hdr[MAXLINE];
        char other_hdrs[MAXLINE];
        char hostname[MAXLINE];
        char port[MAXLINE];
        char path[MAXLINE];
        char request[MAXLINE];
        char cache_object[MAX_OBJECT_SIZE];
        int cacheable=1;
        size_t cache_size=0;
        host_hdr[0] = '\0';
        other_hdrs[0] = '\0';
        request[0] = '\0';
        Rio_readinitb(&rio,connfd);
        n=Rio_readlineb(&rio,buf,MAXLINE);
        if(n>0){
            if(sscanf(buf,"%s %s %s",method,url,version)!=3){
                Close(connfd);
                return;
            };
        }
        else{
            Close(connfd);
            return;
        }
        if(strcmp(method,"GET")!=0){
            printf("暂时不接受这个请求:%s\n",method);
            Close(connfd);
            return;
        }
        parse_url(url,hostname,port,path);
        while(1){
            n=Rio_readlineb(&rio,buf,MAXLINE);
            if(n<=0){
                break;
            }
            if(strcmp(buf,"\r\n")==0){
                break;
            }
            if(strncmp(buf,"Host:",5)==0){
                strcpy(host_hdr,buf);
            }
            else if(strncmp(buf,"User-Agent:",11)==0){
                continue;
            }
            else if(strncmp(buf,"Connection:",11)==0){
                continue;
            }
            else if(strncmp(buf,"Proxy-Connection:",17)==0){
                continue;
            }
            else{strcat(other_hdrs,buf);}

            printf("%s",buf);
        }
        if(cache_find(url,cache_object,&cache_size)){
            Rio_writen(connfd,cache_object,cache_size);
            Close(connfd);
            return;
        }
        snprintf(request,MAXLINE,"%s %s HTTP/1.0\r\n",method,path);
        if(host_hdr[0]=='\0'){
            snprintf(host_hdr,MAXLINE,"Host: %s:%s\r\n",hostname,port);
        }
        strcat(request,host_hdr);
        strcat(request,user_agent_hdr);
        strcat(request,"Connection: close\r\n");
        strcat(request,"Proxy-Connection: close\r\n");
        strcat(request,other_hdrs);
        strcat(request,"\r\n");
        severfd=Open_clientfd(hostname,port);
        if(severfd<0){
            fprintf(stderr,"连接服务器失败");
            Close(connfd);
            return;
        }
        Rio_writen(severfd,request,strlen(request));
        Rio_readinitb(&sever_rio,severfd);
        while((sever_n=Rio_readnb(&sever_rio,sever_buf,MAXBUF))>0){
            Rio_writen(connfd,sever_buf,sever_n);
            if(cacheable&&cache_size+sever_n<=MAX_OBJECT_SIZE){
                memcpy(cache_object+cache_size,sever_buf,sever_n);
                cache_size+=sever_n;
            }
            else{
                cacheable=0;
            }
        }
        if(cacheable){
            cache_insert(url,cache_object,cache_size);
        }
        Close(severfd);
        Close(connfd);
}
void *thread(void*varge){
    int connfd;
    connfd=*(int*)varge;
    Free(varge);
    Pthread_detach(Pthread_self());
    doit(connfd);
    return NULL;
}
int main(int argc,char**argv)
{   
    int listenfd;
    int connfd;
    pthread_t tid;
    int* connfpd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    if(argc!=2){
        printf("输入错误");
        exit(0);
    }
    Signal(SIGPIPE,SIG_IGN);
    cache_init();
    listenfd=Open_listenfd(argv[1]);
    while(1){
        clientlen=sizeof(clientaddr);
        connfd=Accept(listenfd,(SA*)&clientaddr,&clientlen);
        connfpd=Malloc(sizeof(int));
        *connfpd=connfd;
        Pthread_create(&tid,NULL,thread,connfpd);
    }
    return 0;
}

