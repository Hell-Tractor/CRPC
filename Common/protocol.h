#pragma once
#include <asio/co_spawn.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <asio/ip/tcp.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace crpc {
    template<class T> using awaitable = asio::awaitable<T>;

	namespace proto {
		enum request_type : std::uint8_t {
            HEARTBEAT_PACKET,       // 心跳包
            RPC_REQUEST,            // 通用请求
            RPC_RESPONSE,           // 通用响应
            RPC_METHOD_REQUEST,     // 请求方法调用
            RPC_METHOD_RESPONSE,    // 响应方法调用
		};

		class package final {
        private:
			uint8_t  _type;
			uint32_t _seq_id;
			uint32_t _size;
            uint8_t* _data;

        public:
            package() = default;

            package(const package& other) = delete;

            package& operator=(const package& other) = delete;

            package(package&& other) noexcept
                : _type(other._type), _seq_id(other._seq_id), _size(other._size), _data(other._data) {
                other._data = nullptr;
            }

            package(uint8_t type, uint32_t seq_id) 
                : _type(type), _seq_id(seq_id), _size(0), _data(nullptr) {}

            package(uint8_t type, uint32_t seq_id, uint32_t size, const char* data) 
                : _type(type), _seq_id(seq_id), _size(size) {
                _data = new uint8_t[size];
                memcpy(_data, data, _size);
            }

            package(uint8_t type, uint32_t seq_id, const std::string& data)
                : _type(type), _seq_id(seq_id), _size(data.size()) {
                _data = new uint8_t[_size];
                memcpy(_data, data.data(), _size);
            }

            ~package() {
                if (_data) delete[] _data;
            }

            request_type type() const {
				return static_cast<request_type>(_type);
			}

            uint32_t seq_id() const {
                return _seq_id;
            }

            uint32_t size() const {
				return _size;
			}

            const std::string data() const {
                std::string str;
                str.resize(_size);
                memcpy(str.data(), _data, _size);
                return str;
            }

            void read_from(asio::ip::tcp::socket& socket) {
                asio::read(socket, asio::buffer(&_type,   sizeof(_type)));
				asio::read(socket, asio::buffer(&_seq_id, sizeof(_seq_id)));
				asio::read(socket, asio::buffer(&_size,   sizeof(_size)));
                if (_size > 0) {
					_data = new uint8_t[_size];
					asio::read(socket, asio::buffer(_data, _size));
				}
            }

            template<class callback_t>
            void async_read_from(asio::ip::tcp::socket& socket, callback_t callback) {
                asio::async_read(socket, asio::buffer(&_type, sizeof(_type)), [this, &socket, callback](const asio::error_code& ec, std::size_t) {
                    if (ec) return callback(ec);
                    asio::async_read(socket, asio::buffer(&_seq_id, sizeof(_seq_id)), [this, &socket, callback](const asio::error_code& ec, std::size_t) {
                        if (ec) return callback(ec);
                        asio::async_read(socket, asio::buffer(&_size, sizeof(_size)), [this, &socket, callback](const asio::error_code& ec, std::size_t) {
                            if (ec) return callback(ec);
                            if (_size > 0) {
								_data = new uint8_t[_size];
                                asio::async_read(socket, asio::buffer(_data, _size), [this, &socket, callback](const asio::error_code& ec, std::size_t) {
                                    return callback(ec);
								});
							}
                            else return callback(ec);
						});
					});
				}); 
            }

            awaitable<void> await_read_from(asio::ip::tcp::socket& socket) {
				co_await asio::async_read(socket, asio::buffer(&_type,   sizeof(_type)),   asio::use_awaitable);
				co_await asio::async_read(socket, asio::buffer(&_seq_id, sizeof(_seq_id)), asio::use_awaitable);
				co_await asio::async_read(socket, asio::buffer(&_size,   sizeof(_size)),   asio::use_awaitable);
                if (_size > 0) {
					_data = new uint8_t[_size];
					co_await asio::async_read(socket, asio::buffer(_data, _size), asio::use_awaitable);
				}
			}

            void write_to(asio::ip::tcp::socket& socket) const {
				asio::write(socket, asio::buffer(&_type,   sizeof(_type)));
                asio::write(socket, asio::buffer(&_seq_id, sizeof(_seq_id)));
                asio::write(socket, asio::buffer(&_size,   sizeof(_size)));
                if (_size > 0) {
                    asio::write(socket, asio::buffer(_data, _size));
                }
            }

            template<class callback_t>
            void async_write_to(asio::ip::tcp::socket& socket, callback_t callback) {
                asio::async_write(socket, asio::buffer(&_type, sizeof(_type)), [this, &socket, callback](const asio::error_code& ec, std::size_t) {
					if (ec) return callback(ec);
                    asio::async_write(socket, asio::buffer(&_seq_id, sizeof(_seq_id)), [this, &socket, callback](const asio::error_code& ec, std::size_t) {
						if (ec) return callback(ec);
                        asio::async_write(socket, asio::buffer(&_size, sizeof(_size)), [this, &socket, callback](const asio::error_code& ec, std::size_t) {
							if (ec) return callback(ec);
                            if (_size > 0) {
                                asio::async_write(socket, asio::buffer(_data, _size), [this, &socket, callback](const asio::error_code& ec, std::size_t) {
									return callback(ec);
								});
							}
							else return callback(ec);
						});
					});
				}); 
            }

            awaitable<void> await_write_to(asio::ip::tcp::socket& socket) const {
                co_await asio::async_write(socket, asio::buffer(&_type,   sizeof(_type)),   asio::use_awaitable);
                co_await asio::async_write(socket, asio::buffer(&_seq_id, sizeof(_seq_id)), asio::use_awaitable);
                co_await asio::async_write(socket, asio::buffer(&_size,   sizeof(_size)),   asio::use_awaitable);
                if (_size > 0) {
					co_await asio::async_write(socket, asio::buffer(_data, _size), asio::use_awaitable);
				}
            }

            std::string brief_info() const {
				return std::string("type: ") + std::to_string(_type) + ", seq_id: " + std::to_string(_seq_id) + ", size: " + std::to_string(_size);
			}
		};

	}
}