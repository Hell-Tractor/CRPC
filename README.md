# CRPC

A simple RPC frame work made in C++20

## TODO

### Client 客户端

- **connect_registry 连接注册中心**
- **connect_server 连接服务端**
  - 如果有注册中心，则这个函数是private
  - 如果连接池中存在则直接使用，否则新建连接并缓存连接，
  - 每个连接附带一个任务：定期发送心跳包以检测是否可用，如果不可用则删除该连接
- **call 同步调用**
  - （有注册中心）服务发现，路由策略选择，连接服务端
  - 序列化并包装
  - 对当前已连接的服务端发送包
  - 阻塞等待响应
- **call_async 异步调用(std::future)**
  - （有注册中心）服务发现，路由策略选择，连接服务端
  - 序列化并包装
  - 对当前已连接的服务端发送包
  - 返回future，用响应的回调设置future的值
- **call_async_callback 异步调用带回调**
  - （有注册中心）服务发现，路由策略选择，连接
  - 序列化并包装
  - 对当前已连接的服务端发送包
  - 回调挂到响应上
- **run 运行客户端**
  - ...
- **get_services 从注册中心获取服务列表（某个服务可用的服务器）**
  - 从注册中心获取提供某个服务可用的所有服务器列表
- **route 使用路由策略获取服务地址**
  - 随机、轮询、IPHASH


### Server 服务端
- **bind 绑定自身地址**
- **connect_registry 连接注册中心**
- **register_method 注册服务**
  - 在本地注册服务
  - 向注册中心发送新注册信息
- **run 运行服务端**
  - 监听客户端连接
  - 响应客户端的：1.心跳包 2.调用请求

### Registry 注册中心
- **bind 绑定自身地址**
- **run 运行注册中心**
  - 监听客户端和服务端连接
  - 维护服务列表
  - 响应客户端的：服务发现请求
  - 响应服务端的：服务注册请求

### Serializer 序列化器


## 3rdparty

- [cereal](https://github.com/USCiLab/cereal) for serialize and deserialize