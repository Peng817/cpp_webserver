# cpp_webserver
c++多线程服务器练习项目

## 快速运行

- 服务器测试环境

  - Centos7
  - MySQL版本5.7.29

- 测试前确认主机已安装Mysql数据库,及mysql-devel

  ```
  // 建立yourdb库
  create database yourdb;
  
  // 创建user表
  USE yourdb;
  CREATE TABLE user(
      username char(50) NULL,
      password char(50) NULL
  )ENGINE=InnoDB;
  
  // 添加数据
  INSERT INTO user(username, password) VALUES('name', 'passwd');
  ```

- 修改main.cpp中的数据库初始化信息

  ```
  //数据库登录名,密码,库名
  const char *MY_MYSQL_USERNAME = ${your_username};
  const char *MY_MYSQL_PASSWORD = ${your_password};
  const char *MY_MYSQL_DBNAME = ${yourdb};
  ```

- build

  ```
  mkdir build && cd build
  cmake ..
  make .
  ./app ${your_host} ${port}
  ```

  
