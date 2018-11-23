#include <mysql/mysql.h>
#include <libgo/libgo.h>
#include <stdio.h>
#include <exception>
#include <thread>
#include <unistd.h>

void init() {
	try{
		auto m_mysql = mysql_init(NULL);
		if (!m_mysql) {
			printf("mysql_init failed\n");
			return ;
		}

		my_bool reconnect = true;
		mysql_options(m_mysql, MYSQL_OPT_RECONNECT, &reconnect);
		mysql_options(m_mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");

		if (!mysql_real_connect(m_mysql, 
                    "127.0.0.1", "root", "root",
                    "test", 3306,
                    NULL, CLIENT_FOUND_ROWS)) {
			printf("mysql_real_connect failed: %s\n", mysql_error(m_mysql));
			return ;
		}else{
			printf("mysql_real_connect success\n");
		}
	}catch (std::exception & e){
		printf(" ====== CDBConn::Init() ERROR %s\n",e.what());
	}
}

int main() {
    go init;

    // 主线程调度
//    co_sched.Start();

    // 子线程调度
    std::thread([]{
            co_sched.Start();
            }).detach();
    for (;;)
        sleep(1);
}
