#include <cstddef>
#include <mutex>
#include <mysql/mysql.h>
#include <string>
#include "sql_connection_pool.h"
#include "../log/log.h"

//using namespace std;


//构造初始化
void connection_pool::init(const std::string& url, const std::string& User, const std::string& PassWord, const std::string& DataBaseName, const int Port, const int MaxConn)
{
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DataBaseName;

	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = nullptr;
		con = mysql_init(con);

		if (con == nullptr)
		{
			LOG_ERROR("MySQL Error: mysql_init failed");
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DataBaseName.c_str(), Port, NULL, 0);

		if (con == nullptr)
		{
			LOG_ERROR("MySQL Error: mysql_real_connect failed");
			exit(1);
		}
		connList.push_back(con);
		++m_FreeConn;
	}

	m_MaxConn = m_FreeConn;
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	std::unique_lock<std::mutex> lock(m_mutex);

	m_cond.wait(lock, [this] (){return !connList.empty();});

	MYSQL *con = connList.front();
	connList.pop_front();

	--m_FreeConn;
	++m_CurConn;

	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (nullptr == con) return false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		connList.push_back(con);
		++m_FreeConn;
		--m_CurConn;
	}
	
	m_cond.notify_one();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	std::lock_guard<std::mutex> lock(m_mutex);
	if (connList.size() > 0)
	{
		std::list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL *&SQL, connection_pool *connPool){
	SQL = connPool->GetConnection();
	
	conRAII = SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	if (poolRAII != nullptr && conRAII != nullptr) 
    {
        poolRAII->ReleaseConnection(conRAII);
    }
}