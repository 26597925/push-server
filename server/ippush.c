
#ifdef _WIN32  
#define WIN32_LEAN_AND_MEAN  
#include <windows.h>  
#else  
#include <sys/types.h>  
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <netdb.h>  
#include <arpa/inet.h>  
#include <string.h>  
#include <unistd.h>  
#include <fcntl.h>  
#include <errno.h>  
#endif  
  
#include <stdlib.h>  
#include <stdint.h>  
#include <assert.h>  
  
#include <openssl/ssl.h>  
#include <openssl/bio.h>  
#include <openssl/err.h> 
//#include "beanstalk.h" 
#include "ippush.h"  
  


//copy from: 
//http://blog.csdn.net/iw1210/article/details/18085225
//��Linux�±��룺gcc -o ippush ippush.c -lssl  
  
void DeviceToken2Binary(const char* sz, const int len, unsigned char* const binary, const int size)   
{  
    int         i, val;  
    const char*     pin;  
    char        buf[3] = {0};  
  
    assert(size >= TOKEN_SIZE);  
  
    for (i = 0;i < len;i++)  
    {  
        pin = sz + i * 2;  
        buf[0] = pin[0];  
        buf[1] = pin[1];  
  
        val = 0;  
        sscanf(buf, "%X", &val);  
        binary[i] = val;  
    }  
  
    return;  
}  
  
void DeviceBinary2Token(const unsigned char* data, const int len, char* const token, const int size)   
{  
    int i;  
  
    assert(size > TOKEN_SIZE * 2);  
  
    for (i = 0;i < len;i++)  
    {  
        sprintf(token + i * 2, "%02x", data[i]);  
    }  
  
    return;  
}  
  
void Closesocket(int socket)  
{  
#ifdef _WIN32  
    closesocket(socket);  
#else  
    close(socket);  
#endif  
}  
  
// ��ʼ��ssl�⣬Windows�³�ʼ��WinSock  
void init_openssl()  
{  
#ifdef _WIN32  
    WSADATA wsaData;  
    WSAStartup(MAKEWORD(2, 2), &wsaData);  
#endif  
  
    SSL_library_init();  
    ERR_load_BIO_strings();  
    SSL_load_error_strings();  
    OpenSSL_add_all_algorithms();  
}  
  
SSL_CTX* init_ssl_context(  
        const char* clientcert, /* �ͻ��˵�֤�� */  
        const char* clientkey, /* �ͻ��˵�Key */  
        const char* keypwd, /* �ͻ���Key������, ����еĻ� */  
        const char* cacert) /* ������CA֤�� ����еĻ� */  
{  
    // set up the ssl context  
    SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());  
    if (!ctx) {  
    	printf("line:%d filed\n",__LINE__);
        fprintf(stderr, "Error loading trust store\n");
    	ERR_print_errors_fp(stderr);
        return NULL;  
    }  

	
    // certificate  
    if (SSL_CTX_use_certificate_file(ctx, clientcert, SSL_FILETYPE_PEM) <= 0) {  
	    printf("line:%d filed\n",__LINE__);
        return NULL;  
    }  
  
    // key  
    
    if (SSL_CTX_use_PrivateKey_file(ctx, clientkey, SSL_FILETYPE_PEM) <= 0) {  
    	printf("line:%d filed\n",__LINE__);
        return NULL;  
    }  
  	/*
  	if (SSL_use_PrivateKey_ASN1(ctx, clientkey, SSL_FILETYPE_PEM) <= 0) {  
    	printf("line:%d filed\n",__LINE__);
        return NULL;  
    }
	*/ 
    // make sure the key and certificate file match  
    if (SSL_CTX_check_private_key(ctx) == 0) {  
    	printf("line:%d filed\n",__LINE__);
        return NULL;  
    }  
  
    // load ca if exist  
    if (cacert) {  
        if (!SSL_CTX_load_verify_locations(ctx, cacert, NULL)) {  
        	printf("line:%d filed\n",__LINE__);
            return NULL;  
        }  
    }  
  
    return ctx;  
}  
  
// ����TCP���ӵ�������  
int tcp_connect(const char* host, int port)  
{  
    struct hostent *hp;  
    struct sockaddr_in addr;  
    int sock = -1;  
  
    // ��������  
    if (!(hp = gethostbyname(host))) {  
    	printf("line:%d filed\n",__LINE__);
        return -1;  
    }  
  
    memset(&addr, 0, sizeof(addr));  
    addr.sin_addr = *(struct in_addr*)hp->h_addr_list[0];  
    addr.sin_family = AF_INET;  
    addr.sin_port = htons(port);  
  
    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){  
    	printf("line:%d filed\n",__LINE__);
        return -1;  
    }  
  
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {  
    	printf("line:%d filed\n",__LINE__);
        return -1;  
    }  
  
    return sock;  
}  
  
// ʵ��SSL���֣�����SSL����  
SSL* ssl_connect(SSL_CTX* ctx, int socket)  
{  
    SSL *ssl = SSL_new(ctx);  
    BIO *bio = BIO_new_socket(socket, BIO_NOCLOSE);  
    SSL_set_bio(ssl, bio, bio);  
  
    if (SSL_connect(ssl) <= 0) {
		printf("error line:%d",__LINE__);  
        return NULL;  
    }  
  
    return ssl;  
}  
  
// ��֤������֤��  
// ����Ҫ��֤��������֤����Ч�����Ҫ��֤������֤���CommonName(CN)������  
// ʵ��Ҫ���ӵķ���������һ��  
int verify_connection(SSL* ssl, const char* peername)  
{  
    int result = SSL_get_verify_result(ssl);  
    if (result != X509_V_OK) {  
        fprintf(stderr, "WARNING! ssl verify failed: %d", result);  
        return -1;  
    }  
  
    X509 *peer;  
    char peer_CN[256] = {0};  
  
    peer = SSL_get_peer_certificate(ssl);  
    X509_NAME_get_text_by_NID(X509_get_subject_name(peer), NID_commonName, peer_CN, 255);  
    if (strcmp(peer_CN, peername) != 0) {  
        fprintf(stderr, "WARNING! Server Name Doesn't match, got: %s, required: %s", peer_CN,  
                peername);  
    }  
    return 0;  
}  
  
void json_escape(char*  str)  
{  
    int     n;  
    char    buf[1024];  
  
    n = strlen(str) * sizeof(char) + 100;  
    assert(n < sizeof(buf));  
  
    strncpy(buf, str, n);  
    buf[n] = '\0';  
    char *found = buf;  
    while (*found != '\0')  
    {  
        if('\\' == *found || '"' == *found || '\n' == *found || '/' == *found)  
            *str++ = '\\';  
  
        if('\n' == *found)  
            *found = 'n';  
  
        *str++ = *found++;         
    }  
  
    *str='\0';  
  
    return;  
}  
  
// Payload example  
  
// {"aps":{"alert" : "You got your emails.","badge" : 9,"sound" : "default"}}  
int build_payload(char* buffer, int* plen, char* msg, int badage, const char * sound)  
{  
    int n;  
    char buf[2048]={0};  
    char str[2048] = "{\"aps\":{\"alert\":\"";  
  
    n = strlen(str);  
  	printf("xxx:%d,%s",n,str);
    if (msg)   
    {  
        strcpy(buf, msg);  
        json_escape(buf);  
        n = n + sprintf(str+n, "%s", buf);  
        printf("1:%d,,,%s\n",n,str);
    }  
  
    n = n + sprintf(str+n, "%s%d", "\",\"badge\":", badage);  
  	printf("2:......%s\n",str);
    if (sound)   
    {  
        n = n + sprintf(str+n, "%s", ",\"sound\":\"");  
        strcpy(buf, sound);  
        json_escape(buf);  
        n = n + sprintf(str+n, "%s%s", buf, "\"");  
    }  
  	printf("2:%s\n",str);
    strcat(str, "}}");  
  
    n = strlen(str);  
  
    if (n > *plen)   
    {  
        *plen = n;  
        return -1;  
    }  
  
  
    if (n < *plen)   
    {  
        strcpy(buffer, str);  
    } else   
    {  
        strncpy(buffer, str, *plen);  
    }  
  
    *plen = n;  
  	printf("%s\n",str);
  	printf("%s\n",buffer);
    return *plen;  
}  

int build_output_packet_3(char* buf, int buflen, /* ����Ļ����������� */  
        const char* tokenbinary, /* �����Ƶ�Token */  
        char* msg){ /* �豸�յ�ʱ���ŵ�����������Ϊ�� */  
      assert(buflen >= 1 + 2 + TOKEN_SIZE + 2 + MAX_PAYLOAD_SIZE);  
  
    char * pdata = buf;  
    // command  
    *pdata = 0;  
  
    // token length  
    pdata++;  
    *(uint16_t*)pdata = htons(TOKEN_SIZE);  
  
    // token binary  
    pdata += 2;  
    memcpy(pdata, tokenbinary, TOKEN_SIZE);  
  
    pdata += TOKEN_SIZE;  
  
    int payloadlen = MAX_PAYLOAD_SIZE; 
	/* 
    if (build_payload(pdata + 2, &payloadlen, msg, badage, sound) < 0)   
    {  
        msg[strlen(msg) - (payloadlen - MAX_PAYLOAD_SIZE)] = '\0';  
        payloadlen = MAX_PAYLOAD_SIZE;  
        if (build_payload(pdata + 2, &payloadlen, msg, badage, sound) <= 0)   
        {  
            return -1;  
        }  
    }  
    */
    memcpy(pdata+2,msg,strlen(msg));
    *(uint16_t*)pdata = htons(strlen(msg));    
    return 1 + 2 + TOKEN_SIZE + 2 + strlen(msg);     	
        	
}
int send_message_3(SSL *ssl, const char* token, char* msg)  
{  
    int         n;  
    char        buf[1 + 2 + TOKEN_SIZE + 2 + MAX_PAYLOAD_SIZE];  
    unsigned char   binary[TOKEN_SIZE];  
    int         buflen = sizeof(buf);  
    bzero(buf,1 + 2 + TOKEN_SIZE + 2 + MAX_PAYLOAD_SIZE);
  
    n = strlen(token);  
    DeviceToken2Binary(token, n, binary, TOKEN_SIZE);  
    printf("binary:\n");
  	dump_data(binary,TOKEN_SIZE);
    buflen = build_output_packet_3(buf, buflen, (const char*)binary, msg);  
    
    if (buflen <= 0) {  
        return -1;  
    }  
    printf("buf:\n");
  	dump_data(buf,buflen);
    return SSL_write(ssl, buf, buflen);  
}    
// ��һ����ʽ�İ�  
int build_output_packet(char* buf, int buflen, /* ����Ļ����������� */  
        const char* tokenbinary, /* �����Ƶ�Token */  
        char* msg, /* Ҫ���͵���Ϣ */  
        int badage, /* Ӧ��ͼ������ʾ������ */  
        const char * sound) /* �豸�յ�ʱ���ŵ�����������Ϊ�� */  
{  
    assert(buflen >= 1 + 2 + TOKEN_SIZE + 2 + MAX_PAYLOAD_SIZE);  
  
    char * pdata = buf;  
    // command  
    *pdata = 0;  
  
    // token length  
    pdata++;  
    *(uint16_t*)pdata = htons(TOKEN_SIZE);  
  
    // token binary  
    pdata += 2;  
    memcpy(pdata, tokenbinary, TOKEN_SIZE);  
  
    pdata += TOKEN_SIZE;  
  
    int payloadlen = MAX_PAYLOAD_SIZE;  
    if (build_payload(pdata + 2, &payloadlen, msg, badage, sound) < 0)   
    {  
        msg[strlen(msg) - (payloadlen - MAX_PAYLOAD_SIZE)] = '\0';  
        payloadlen = MAX_PAYLOAD_SIZE;  
        if (build_payload(pdata + 2, &payloadlen, msg, badage, sound) <= 0)   
        {  
            return -1;  
        }  
    }  
    *(uint16_t*)pdata = htons(payloadlen);  
  
    return 1 + 2 + TOKEN_SIZE + 2 + payloadlen;  
}  
  
int send_message(SSL *ssl, const char* token, char* msg, int badage, const char* sound)  
{  
    int         n;  
    char        buf[1 + 2 + TOKEN_SIZE + 2 + MAX_PAYLOAD_SIZE];  
    unsigned char   binary[TOKEN_SIZE];  
    int         buflen = sizeof(buf);  
  
    n = strlen(token);  
    DeviceToken2Binary(token, n, binary, TOKEN_SIZE);  
  
    buflen = build_output_packet(buf, buflen, (const char*)binary, msg, badage, sound);  
    if (buflen <= 0) {  
        return -1;  
    }  
  
    return SSL_write(ssl, buf, buflen);  
}  
  
int build_output_packet_2(char* buf, int buflen, /* ������������ */  
        uint32_t messageid, /* ��Ϣ��� */  
        uint32_t expiry, /* ����ʱ�� */  
        const char* tokenbinary, /* ������Token */  
        char* msg, /* message */  
        int badage, /* badage */  
        const char * sound) /* sound */  
{  
    assert(buflen >= 1 + 4 + 4 + 2 + TOKEN_SIZE + 2 + MAX_PAYLOAD_SIZE);  
  
    char * pdata = buf;  
    // command  
    *pdata = 1;  
  
    // messageid  
    pdata++;  
    *(uint32_t*)pdata = messageid;  
  
    // expiry time  
    pdata += 4;  
    *(uint32_t*)pdata = htonl(expiry);  
  
    // token length  
    pdata += 4;  
    *(uint16_t*)pdata = htons(TOKEN_SIZE);  
  
    // token binary  
    pdata += 2;  
    memcpy(pdata, tokenbinary, TOKEN_SIZE);  
  
    pdata += TOKEN_SIZE;  
  
    int payloadlen = MAX_PAYLOAD_SIZE;  
    if (build_payload(pdata + 2, &payloadlen, msg, badage, sound) < 0)   
    {  
        msg[strlen(msg) - (payloadlen - MAX_PAYLOAD_SIZE)] = '\0';  
        payloadlen = MAX_PAYLOAD_SIZE;  
        if (build_payload(pdata + 2, &payloadlen, msg, badage, sound) <= 0)   
        {  
            return -1;  
        }  
    }  
  
    *(uint16_t*)pdata = htons(payloadlen);  
  
    return 1 + 4 + 4 + 2 + TOKEN_SIZE + 2 + payloadlen;  
}  
  
int send_message_2(SSL *ssl, const char* token, uint32_t id, uint32_t expire, char* msg, int badage, const char* sound)  
{  
    int         i, n;  
    char buf[1 + 4 + 4 + 2 + TOKEN_SIZE + 2 + MAX_PAYLOAD_SIZE];  
    unsigned char   binary[TOKEN_SIZE];  
    int buflen = sizeof(buf);  
  
    n = strlen(token);  
    printf("token length : %d, TOKEN_SIZE = %d\n token = %s\n", n, TOKEN_SIZE, token);  
    DeviceToken2Binary(token, n, binary, TOKEN_SIZE);  
  
    for (i = 0; i < TOKEN_SIZE; i++)  
        printf("%d ", binary[i]);  
    printf("\n");  
  
  
    buflen = build_output_packet_2(buf, buflen, id, expire,(const char*)binary, msg, badage, sound);  
  
    if (buflen <= 0) {  
        return -1;  
    }  
  
    n = SSL_write(ssl, buf, buflen);  
  
    return  n;  
}  
char *trim(char *str,char trimchar)
{
        char *p = str;
        char *p1;
        if(p)
        {
                p1 = p + strlen(str) - 1;
                while(*p && isspace(*p)) p++;
                while(p1 > p && isspace(*p1)) *p1-- = trimchar;//'/0';
        }
        return p;
}
#define __DEBUG(format, ...) printf("FILE: "__FILE__", LINE: %d: "format"/n", __LINE__, ##__VA_ARGS__)
int pushMessage(char* host,
				int port,
				const char* clientcert,
	 			const char* clientkey, /* �ͻ��˵�Key */  
        		const char* keypwd, /* �ͻ���Key������, ����еĻ� */  
        		const char* cacert, /* ������CA֤�� ����еĻ� */  
        		//int badage, const char *sound,
				push_message_t*(*getMessageInfo)(void* args),void* args,
				void(*sendMssageOK)(int result,long long job)){
	int  i, n;  
    char buf[1024];  
    char * msg = NULL;  
	// ��ʼ��Context  
    // develop.pem�����ǵ�֤���Key��Ϊ�˷���ʹ�ã����ǰ�֤���Keyд��ͬһ���ļ���  
    // ��ȡˮ��Key�����뱣��  
    // entrust_2048_ca.pem��ƻ��֤���CA����������Openssl�ĸ�֤���У�������Ҫ�����ֶ�ָ������Ȼ���޷���֤  
    // ��ϸ��http://www.entrust.net/developer/index.cfm  
    __DEBUG("\n");
    SSL_CTX *ctx = init_ssl_context(clientcert,
									 clientkey, 
									 keypwd,
									 cacert);  
	if (!ctx) {  
        fprintf(stderr, "init ssl context failed: %s\n",  
        ERR_reason_error_string(ERR_get_error()));  
        return -1;  
    } 	
        __DEBUG("\n");
	int socket = tcp_connect(host, port);  
    if (socket < 0) {  
        fprintf(stderr, "failed to connect to host %s\n",  
                strerror(errno));
		ERR_print_errors_fp(stderr);
        return -1;  
    }  		
	// SSL����  
    SSL *ssl = ssl_connect(ctx, socket);      __DEBUG("\n");
    if (!ssl) {  
    	 __DEBUG("\n");
        fprintf(stderr, "ssl connect failed: %s\n",  
                ERR_reason_error_string(ERR_get_error()));  
        ERR_print_errors_fp(stderr);
        Closesocket(socket);  
        return -1;  
    }  
      __DEBUG("\n");
    // ��֤������֤��  
    if (verify_connection(ssl, host) != 0) {  
        fprintf(stderr, "verify failed\n");  
		ERR_print_errors_fp(stderr);
        Closesocket(socket);  
        return -1;  
    }
	  					    __DEBUG("\n");
    push_message_t* message = getMessageInfo(args);
   
    while(message){    __DEBUG("\n");
		printf("%s%d get message is %s",__FILE__,__LINE__,message->message);
    	n = send_message_3(ssl,message->token,message->message);//,badage,sound);
    	//free(message->token);
    	//free(message->message);
    	free(message);	    	    
	    if (n <= 0)   
	    {  
	        fprintf(stderr, "send failed: %s\n", ERR_reason_error_string(ERR_get_error()));  
	    }else  
	    {  
	        printf("send sucessfully:%d.\n",n);  
	    }    
   	    sendMssageOK(n,message->id);
	    // ����ƻ�����ͷ��������������ݣ�      
	    n = recv(socket, buf, sizeof(buf), MSG_DONTWAIT); //������ģʽ����    
	    /*
	    //n = read(socket, buf, sizeof(buf)); ////����ģʽ����  
	    printf("from APNS, n = %d\n", n);  
	    for(i=0; i<n; i++)  
	        printf("%d ", buf[i]);  
	    printf("\n");  
	    */
	    message = getMessageInfo(args);
    }	  
    // �ر�����  
    SSL_shutdown(ssl);  
    Closesocket(socket); 	 
    return 0;
}


int pushMessage_2(char* host,
				int port,
				const char* clientcert,
	 			const char* clientkey, /* �ͻ��˵�Key */  
        		const char* keypwd, /* �ͻ���Key������, ����еĻ� */  
        		const char* cacert, /* ������CA֤�� ����еĻ� */  
        		const char* token,const char* message){
	int  i, n;  
    char buf[1024];  
    char * msg = NULL;  
	// ��ʼ��Context  
    // develop.pem�����ǵ�֤���Key��Ϊ�˷���ʹ�ã����ǰ�֤���Keyд��ͬһ���ļ���  
    // ��ȡˮ��Key�����뱣��  
    // entrust_2048_ca.pem��ƻ��֤���CA����������Openssl�ĸ�֤���У�������Ҫ�����ֶ�ָ������Ȼ���޷���֤  
    // ��ϸ��http://www.entrust.net/developer/index.cfm  
    __DEBUG("\n");
    SSL_CTX *ctx = init_ssl_context(clientcert,
									 clientkey, 
									 keypwd,
									 cacert);  
	if (!ctx) {  
        fprintf(stderr, "init ssl context failed: %s\n",  
        ERR_reason_error_string(ERR_get_error()));  
        return -1;  
    } 	
        __DEBUG("\n");
	int socket = tcp_connect(host, port);  
    if (socket < 0) {  
        fprintf(stderr, "failed to connect to host %s\n",  
                strerror(errno));
		ERR_print_errors_fp(stderr);
        return -1;  
    }  		
	// SSL����  
    SSL *ssl = ssl_connect(ctx, socket);      __DEBUG("\n");
    if (!ssl) {  
    	 __DEBUG("\n");
        fprintf(stderr, "ssl connect failed: %s\n",  
                ERR_reason_error_string(ERR_get_error()));  
        ERR_print_errors_fp(stderr);
        Closesocket(socket);  
        return -1;  
    }  
      __DEBUG("\n");
    // ��֤������֤��  
    if (verify_connection(ssl, host) != 0) {  
        fprintf(stderr, "verify failed\n");  
		ERR_print_errors_fp(stderr);
        Closesocket(socket);  
        return -1;  
    }

   
    if(message){    
		
    	n = send_message_3(ssl, token, message);//,badage,sound);	    	    
	    if (n <= 0)   
	    {  
	        fprintf(stderr, "send failed: %s\n", ERR_reason_error_string(ERR_get_error()));  
	    }else  
	    {  
	        printf("send sucessfully:%d.\n",n);  
	    }    
   	    
	    // ����ƻ�����ͷ��������������ݣ�      
	    n = recv(socket, buf, sizeof(buf), MSG_DONTWAIT); //������ģʽ����    
	    /*
	    //n = read(socket, buf, sizeof(buf)); ////����ģʽ����  
	    printf("from APNS, n = %d\n", n);  
	    for(i=0; i<n; i++)  
	        printf("%d ", buf[i]);  
	    printf("\n");  
	    */
    }	  
    // �ر�����  
    SSL_shutdown(ssl);  
    Closesocket(socket); 	 
    return 0;
}
/*
int main(int argc, char** argv)  
{  
    int  i, n;  
    char buf[1024];  
    char * msg = NULL;  
    if (argc > 1) msg = argv[1];  
  
    init_openssl();  
  
    // ��ʼ��Context  
    // develop.pem�����ǵ�֤���Key��Ϊ�˷���ʹ�ã����ǰ�֤���Keyд��ͬһ���ļ���  
    // ��ȡˮ��Key�����뱣��  
    // entrust_2048_ca.pem��ƻ��֤���CA����������Openssl�ĸ�֤���У�������Ҫ�����ֶ�ָ������Ȼ���޷���֤  
    // ��ϸ��http://www.entrust.net/developer/index.cfm  
    SSL_CTX *ctx = init_ssl_context("20141001-PRO-Cert.pem",
									 "20141001-PRO-Key.pem", 
									 "1234",
									  "Entrust.pem");  
    if (!ctx) {  
        fprintf(stderr, "init ssl context failed: %s\n",  
                ERR_reason_error_string(ERR_get_error()));  
        return -1;  
    }  
  
    // ���ӵ����Է�����  
    const char* host = "gateway.push.apple.com";  
    const int port = 2195;  
    int socket = tcp_connect(host, port);  
    if (socket < 0) {  
        fprintf(stderr, "failed to connect to host %s\n",  
                strerror(errno));  
        return -1;  
    }  
  
    // SSL����  
    SSL *ssl = ssl_connect(ctx, socket);  
    if (!ssl) {  
        fprintf(stderr, "ssl connect failed: %s\n",  
                ERR_reason_error_string(ERR_get_error()));  
        Closesocket(socket);  
        return -1;  
    }  
  
    // ��֤������֤��  
    if (verify_connection(ssl, host) != 0) {  
        fprintf(stderr, "verify failed\n");  
        Closesocket(socket);  
        return 1;  
    }  
  
    uint32_t msgid = 1;  
    uint32_t expire = time(NULL) + 24 * 3600; // expire 1 day  
  
    printf("main, expire = %d\n", expire);  
  
    if (!msg) {  
        msg = "hello\nThis is a test message";  
    }  
  
    // ����һ����Ϣ  
    const char* token = "9e65499f274190e2bdc7a233e6414454febc77898cbea7e3dc35d6c3e5d71dd7";  
    int j =0;
    char msgs[255] = {0};
    strcpy(msgs,msg);
    ///////////////////////////////////////////////
    
    int fd = bs_connect("127.0.0.1",11300);
    printf("bs %d,%d,%s\n",fd,errno,strerror(errno));
    bs_use(fd,"TEST2");
    
	strcpy(msgs,msg);    
	char bufx[MAXBUF]="hahanilaibulai";
    for(j=0;j<10;j++)
	{    	
    	//int len = strlen(msgs);
    	//msgs[len] = '0'+j;
    	fgets(bufx, MAXBUF, stdin);
    	int len = strlen(bufx);
    	printf("input len:%d\n",len);
    //	bufx[len/2]='\n';
    	bs_put(fd,0,0,120,bufx,len);
    }
    bzero(msgs,255);
        
    bs_watch(fd,"TEST2");
    bs_ignore(fd,"default");
    //////////////////////////////////////////////
    for(j=0;j<10;j++)
	{
    	BSJ* retbsj;
    	int ret = bs_reserve_with_timeout(fd,1,&retbsj);
    	int len = strlen(retbsj->data);
    	dump_data(retbsj->data,retbsj->size);
    	bzero(msgs,255);
    	printf("-----neirong----\n%s\n",retbsj->data);
    	memcpy(msgs, retbsj->data,retbsj->size);
    	msgs[retbsj->size] = '\0';    	
    	dump_data(msgs,strlen(msgs));
    	char* test = trim(msgs,'\n');
    	printf("------start send--%s---------------\n",test);
	    n = send_message_2(ssl, token, msgid++, expire, test, 1, "default");  
	    //n = send_message(ssl,token,msgs,1,"default");
	    bs_delete(fd,retbsj->id);
	    printf("after send_message_2, n = %d\n", n);  
	    if (n <= 0)   
	    {  
	        fprintf(stderr, "send failed: %s\n", ERR_reason_error_string(ERR_get_error()));  
	    }else  
	    {  
	        printf("send sucessfully:%d.\n",n);  
	    }    
    // ����ƻ�����ͷ��������������ݣ�  
    
	    n = recv(socket, buf, sizeof(buf), MSG_DONTWAIT); //������ģʽ����    
	    
	    //n = read(socket, buf, sizeof(buf)); ////����ģʽ����  
	    printf("from APNS, n = %d\n", n);  
	    for(i=0; i<n; i++)  
	        printf("%d ", buf[i]);  
	    printf("\n");  
	  
	    
 	}
	  
    // �ر�����  
    SSL_shutdown(ssl);  
    Closesocket(socket);  
  
    printf("exit\n");  
  
    return 0;  
}  
*/