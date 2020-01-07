   #include <stdio.h>
   #include <stdlib.h>
   #include <unistd.h>
  #include <string.h>
  #define  SIZE  1024
  void  GetQueryString(char  output[]){
         //按照CGI的协议来实现此处的逻辑
         //1.先获取到方法
         char* method=getenv("REQUEST_METHOD");
         if (method==NULL){
                 //没有获取到环境变量
                 fprintf(stderr,"REQUEST_METHOD  filed\n");
                 return ;
          }
fprintf(stderr,"REQUEST_METHOD  %s\n",method);
          if (strcmp(method,"GET")==0){
                  //获取QUERY_STRING
                  char *query_string =getenv("QUERY_STRING");
                 if (query_string==NULL){
                         fprintf(stderr,"QUERY_STRING  failed\n");
fprintf(stderr,"QUERY_STRING  %s\n",query_string);
                         return ;
                 }
                strcpy(output,query_string);
          }else{
  //post
 //获取CONTENT_LENGTH
  char*  content_length_env=getenv("CONTENT_LENGTH");
  if (content_length_env==NULL){
  fprintf(stderr,"CENTENT_LENGTH   failed\n");
  return ;
 }
  int content_length=atoi(content_length_env);
  int i=0;
  for (;i<content_length;++i){
  char  c='\0';
  read(0,&c,1);
  output[i]=c;
  }
  return ;
  }
  }
 
 int main (){
         //1.基于CGI协议获取到需要的参数
          char  query_string[SIZE]={0};
         GetQueryString(query_string);
         //2.根据业务逻辑进行计算
          //此时时获取到的QUERY_STRING的形式如下
         //a=10&b=20
        int a,b;
         sscanf(query_string ,"a=%d&b=%d\n",&a,&b);
        fprintf(stderr,"a:%d\n",a);
        fprintf(stderr,"b:%d\n",b);
         //3.把结果构造成HTML写回到标准输出
int sum=a*b;
	 printf("Content-type:text/html\n\n");
         printf ("<html><div align='center' style='font-size:50px'><h1>%d x %d =%d</h1></div></html>",a,b,sum);
         return 0;
  }
 
