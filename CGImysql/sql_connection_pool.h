#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <list>
#include <mysql/mysql.h>
#include <string>
#include <mutex>
#include <condition_variable>

//using namespace std;

class connection_pool
{
public:
	static connection_pool *GetInstance() {
        static connection_pool connPool;
        return &connPool;
    }
    
    connection_pool(const connection_pool&) = delete;
    connection_pool& operator=(const connection_pool&) = delete;

	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接
	void DestroyPool();					 //销毁所有连接

	void init(const std::string& url, const std::string& User, 
              const std::string& PassWord, const std::string& DataBaseName, 
              int Port, int MaxConn);

private:
	connection_pool(): m_MaxConn(0), m_CurConn(0), m_FreeConn(0){}
	~connection_pool();

	int m_MaxConn;  //最大连接数
	int m_CurConn;  //当前已使用的连接数
	int m_FreeConn; //当前空闲的连接数
	
	std::mutex m_mutex;
	std::condition_variable m_cond;

	std::list<MYSQL *> connList; //连接池

	std::string m_url;			 //主机地址
	int m_Port;		 //数据库端口号
	std::string m_User;		 //登陆数据库用户名
	std::string m_PassWord;	 //登陆数据库密码
	std::string m_DatabaseName; //使用数据库名
};

class connectionRAII{

public:
	connectionRAII(MYSQL *&con, connection_pool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif
