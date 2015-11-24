#ifndef MYSQLUTIL_H_
#define MYSQLUTIL_H_


typedef struct mysql_row_result{
	int rowid;
	int columncount;
	struct mysql_column_result_t* columns;
	struct mysql_row_result* next;
}mysql_row_result_t;

typedef struct mysql_column_result{
	int columnid;
	char columnname[255];
	char* content;
	struct mysql_result* next;
}mysql_column_result_t;
//�������ӳ� 
int createMysqlPool(int max_pool);
void destroyMysqlPool();
//Ĭ������ 
void init(char* serverip,
					char* dbuser,
					char* pass,
					char* dbname, 
					int port,
					int attri,
					int masterOrSalve);
//��������б�
//void addserverList(char* serverip,char* dbuser,char* pass,char* dbname, int port,	int attri,int masterOrSalve);
//���һ������ 
int getConnection(char* serverip,
					char* dbuser,
					char* pass,
					char* dbname,
				    int port,
					int attri,
					int masterOrSalve);
					
int getConnectByDBType(int masterOrSalve);
int getConnect(); 
//ִ��update��insert 
int execute(int fd,char* sql);
int executeEx(int fd,char* sqlformat,...);	
//���벢����key 
long long  insert(int fd,char* sql,...);
int commit(int fd);
//ִ��select ,���ؼ�¼����,�ͽ���� 
int queryResult(int fd,mysql_row_result_t** result,char* sqlformat,...);
//����������� 
char* getContentById(int row,int columnid,const mysql_row_result_t* result);
char* getContentByName(int row,char* columnname,const mysql_row_result_t* result);
char** getContent2Array(int row,const mysql_row_result_t* result,char** des);
void freeArray(char** src,int columcount);
void freeResult(mysql_row_result_t* result);
//�������ӳ� 
void returnMysql(int fd);//close mysql

#endif /* !MYSQLUTIL */
				