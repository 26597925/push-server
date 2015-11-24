#ifndef IPPUSH_H
#define IPPUSH_H

#define MAX_PAYLOAD_SIZE 256  
#define TOKEN_SIZE 32  

typedef struct push_message{
	long long id;
	char token[64];
	char message[2048];
}push_message_t;

int pushMessage_2(char* host,
				int port,
				const char* clientcert,
	 			const char* clientkey, /* �ͻ��˵�Key */  
        		const char* keypwd, /* �ͻ���Key������, ����еĻ� */  
        		const char* cacert, /* ������CA֤�� ����еĻ� */  
        		const char* token,const char* message);

int pushMessage(char* host,
				int port,
				const char* clientcert,
	 			const char* clientkey, /* �ͻ��˵�Key */  
        		const char* keypwd, /* �ͻ���Key������, ����еĻ� */  
        		const char* cacert, /* ������CA֤�� ����еĻ� */  
        		//int badage, const char *sound,
				push_message_t*(*getMessageInfo)(void* args),void* args,
				void(*sendMssageOK)(int result,long long job));
				
				
				
///////////////////////////////////////////////////////////////
// next is privte method
////////////////////////////////////////////////////////////

void DeviceToken2Binary(const char* sz, const int len, unsigned char* const binary, const int size) ;
void DeviceBinary2Token(const unsigned char* data, const int len, char* const token, const int size) ;
void Closesocket(int socket) ;
void init_openssl() ;
SSL_CTX* init_ssl_context(  
        const char* clientcert, /* �ͻ��˵�֤�� */  
        const char* clientkey, /* �ͻ��˵�Key */  
        const char* keypwd, /* �ͻ���Key������, ����еĻ� */  
        const char* cacert); /* ������CA֤�� ����еĻ� */  
int tcp_connect(const char* host, int port) ;
SSL* ssl_connect(SSL_CTX* ctx, int socket) ;
int verify_connection(SSL* ssl, const char* peername);
void json_escape(char*  str);
int build_payload(char* buffer, int* plen, char* msg, int badage, const char * sound);
int build_output_packet(char* buf, int buflen, /* ����Ļ����������� */  
        const char* tokenbinary, /* �����Ƶ�Token */  
        char* msg, /* Ҫ���͵���Ϣ */  
        int badage, /* Ӧ��ͼ������ʾ������ */  
        const char * sound); /* �豸�յ�ʱ���ŵ�����������Ϊ�� */         
int send_message(SSL *ssl, const char* token, char* msg, int badage, const char* sound)  ;
int build_output_packet_2(char* buf, int buflen, /* ������������ */  
        uint32_t messageid, /* ��Ϣ��� */  
        uint32_t expiry, /* ����ʱ�� */  
        const char* tokenbinary, /* ������Token */  
        char* msg, /* message */  
        int badage, /* badage */  
        const char * sound) ;/* sound */          
int send_message_2(SSL *ssl, const char* token, uint32_t id, uint32_t expire, char* msg, int badage, const char* sound);
int send_message_3(SSL *ssl, const char* token, char* msg);
char *trim(char *str,char trimchar);

#endif /* !IPPUSH_H */