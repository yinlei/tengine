#include "network.hpp"

#include "context.hpp"
#include "executor.hpp"
#include "sandbox.hpp"

#include "asio/use_future.hpp"
#include "asio/steady_timer.hpp"
#include "asio/deadline_timer.hpp"
#include "asio/ts/executor.hpp"

#include <iostream>
#include <chrono>
#include <unordered_map>
#include <map>
#include <random>
//#include <xutility>
#include <chrono>
#include <regex>
#include <future>
#include <thread>

namespace
{
	template <class socket_type>
	class ClientBase {
	public:
		virtual ~ClientBase() {}

		class Response {
			friend class ClientBase<socket_type>;

			class iequal_to {
			public:
				bool operator()(const std::string &key1, const std::string &key2) const {
					return key1 == key2;
				}
			};
			class ihash {
			public:
				size_t operator()(const std::string &key) const {
					std::size_t seed = 0;
					for (auto &c : key)
						seed ^= (uint8_t)std::tolower(c) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
					return seed;
				}
			};
		public:
			std::string http_version, status_code;

			std::istream content;

			std::unordered_multimap<std::string, std::string, ihash, iequal_to> header;

		private:
			asio::streambuf content_buffer;

			Response() : content(&content_buffer) {}
		};

		std::shared_ptr<Response> request(const std::string& request_type, const std::string& path = "/", const std::string& content = "",
			const std::map<std::string, std::string>& header = std::map<std::string, std::string>()) {
			std::string corrected_path = path;
			if (corrected_path == "")
				corrected_path = "/";

			asio::streambuf write_buffer;
			std::ostream write_stream(&write_buffer);
			write_stream << request_type << " " << corrected_path << " HTTP/1.1\r\n";
			write_stream << "Host: " << host << "\r\n";
			for (auto& h : header) {
				write_stream << h.first << ": " << h.second << "\r\n";
			}
			if (content.size()>0)
				write_stream << "Content-Length: " << content.size() << "\r\n";
			write_stream << "\r\n";

			try {
				connect();

				asio::write(*socket, write_buffer);
				if (content.size()>0)
					asio::write(*socket, asio::buffer(content.data(), content.size()));

			}
			catch (const std::exception& e) {
				socket_error = true;
				throw std::invalid_argument(e.what());
			}

			return request_read();
		}

		std::shared_ptr<Response> request(const std::string& request_type, const std::string& path, std::iostream& content,
			const std::map<std::string, std::string>& header = std::map<std::string, std::string>()) {
			std::string corrected_path = path;
			if (corrected_path == "")
				corrected_path = "/";

			content.seekp(0, std::ios::end);
			auto content_length = content.tellp();
			content.seekp(0, std::ios::beg);

			asio::streambuf write_buffer;
			std::ostream write_stream(&write_buffer);
			write_stream << request_type << " " << corrected_path << " HTTP/1.1\r\n";
			write_stream << "Host: " << host << "\r\n";
			for (auto& h : header) {
				write_stream << h.first << ": " << h.second << "\r\n";
			}
			if (content_length>0)
				write_stream << "Content-Length: " << content_length << "\r\n";
			write_stream << "\r\n";
			if (content_length>0)
				write_stream << content.rdbuf();

			try {
				connect();

				asio::write(*socket, write_buffer);
			}
			catch (const std::exception& e) {
				socket_error = true;
				throw std::invalid_argument(e.what());
			}

			return request_read();
		}

	protected:
		asio::io_service asio_io_service;
		asio::ip::tcp::endpoint asio_endpoint;
		asio::ip::tcp::resolver asio_resolver;

		std::shared_ptr<socket_type> socket;
		bool socket_error;

		std::string host;
		unsigned short port;

		ClientBase(const std::string& host_port, unsigned short default_port) :
			asio_resolver(asio_io_service), socket_error(false) {
			size_t host_end = host_port.find(':');
			if (host_end == std::string::npos) {
				host = host_port;
				port = default_port;
			}
			else {
				host = host_port.substr(0, host_end);
				port = static_cast<unsigned short>(stoul(host_port.substr(host_end + 1)));
			}

			asio_endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);
		}

		virtual void connect() = 0;

		void parse_response_header(std::shared_ptr<Response> response, std::istream& stream) const {
			std::string line;
			getline(stream, line);
			size_t version_end = line.find(' ');
			if (version_end != std::string::npos) {
				if (5<line.size())
					response->http_version = line.substr(5, version_end - 5);
				if ((version_end + 1)<line.size())
					response->status_code = line.substr(version_end + 1, line.size() - (version_end + 1) - 1);

				getline(stream, line);
				size_t param_end;
				while ((param_end = line.find(':')) != std::string::npos) {
					size_t value_start = param_end + 1;
					if ((value_start)<line.size()) {
						if (line[value_start] == ' ')
							value_start++;
						if (value_start<line.size())
							response->header.insert(std::make_pair(line.substr(0, param_end), line.substr(value_start, line.size() - value_start - 1)));
					}

					getline(stream, line);
				}
			}
		}

		std::shared_ptr<Response> request_read() {
			std::shared_ptr<Response> response(new Response());

			try {
				size_t bytes_transferred = asio::read_until(*socket, response->content_buffer, "\r\n\r\n");

				size_t num_additional_bytes = response->content_buffer.size() - bytes_transferred;

				parse_response_header(response, response->content);

				auto header_it = response->header.find("Content-Length");
				if (header_it != response->header.end()) {
					auto content_length = stoull(header_it->second);
					if (content_length>num_additional_bytes) {
						asio::read(*socket, response->content_buffer,
							asio::transfer_exactly(content_length - num_additional_bytes));
					}
				}
				else if ((header_it = response->header.find("Transfer-Encoding")) != response->header.end() && header_it->second == "chunked") {
					asio::streambuf streambuf;
					std::ostream content(&streambuf);

					std::streamsize length;
					std::string buffer;
					do {
						size_t bytes_transferred = asio::read_until(*socket, response->content_buffer, "\r\n");
						std::string line;
						getline(response->content, line);
						bytes_transferred -= line.size() + 1;
						line.pop_back();
						length = stol(line, 0, 16);

						auto num_additional_bytes = static_cast<std::streamsize>(response->content_buffer.size() - bytes_transferred);

						if ((2 + length)>num_additional_bytes) {
							asio::read(*socket, response->content_buffer,
								asio::transfer_exactly(2 + length - num_additional_bytes));
						}

						buffer.resize(static_cast<size_t>(length));
						response->content.read(&buffer[0], length);
						content.write(&buffer[0], length);

						//Remove "\r\n"
						response->content.get();
						response->content.get();
					} while (length>0);

					std::ostream response_content_output_stream(&response->content_buffer);
					response_content_output_stream << content.rdbuf();
				}
			}
			catch (const std::exception& e) {
				socket_error = true;
				throw std::invalid_argument(e.what());
			}

			return response;
		}
	};

	template<class socket_type>
	class Client : public ClientBase<socket_type> {};

	typedef asio::ip::tcp::socket HTTP;

	template<>
	class Client<HTTP> : public ClientBase<HTTP> {
	public:
		Client(const std::string& server_port_path) : ClientBase<HTTP>::ClientBase(server_port_path, 80) {
			socket = std::make_shared<HTTP>(asio_io_service);
		}

	private:
		void connect() {
			if (socket_error || !socket->is_open()) {
				asio::ip::tcp::resolver::query query(host, std::to_string(port));
				asio::connect(*socket, asio_resolver.resolve(query));

				asio::ip::tcp::no_delay option(true);
				socket->set_option(option);

				socket_error = false;
			}
		}
	};

	typedef Client<HTTP> HttpClient;
}


namespace tengine
{
	Network::Network(Context& context)
		: Service(context)
		, executor_(new Executor())
	{

	}

	Network::~Network()
	{
		delete executor_;
	}

	int Network::init(const char* name)
	{
		Service::init(name);

		context_.register_name(this, "Network");

		executor_->run();

		return 0;
	}

	const std::string Network::do_request(const std::string& type, const std::string& url, const std::string& path, const std::string& content)
	{
		HttpClient client(url);

		std::stringstream str;

		try {
			auto r = client.request(type, path, content);

			str << r->content.rdbuf();
		}
		catch (const std::exception& e) {
			str << e.what();
		}

		return str.str();
	}

	const std::string Network::sync_request(const char *type, const char *url, const char *path, const char* content)
	{
		return do_request(type, url, path, content);
	}

	void Network::async_request(const char *type, const char *url, const char *path, const char* content, Handler handler)
	{
		std::string request_type(type);
		std::string request_url(url);
		std::string request_path(path);
		std::string request_content(content);

		asio::post(executor(),
			[=]
		{
			asio::post(executor_->executor(),
				[=]
			{
				auto r = this->do_request(request_type, request_url,
					request_path, request_content);

				handler(std::move(r));
			});
		});
	}
}

///////////////////////////////////////////////////////////////////////////////

namespace
{
	template <class socket_type>
	class ServerBase {
	public:
		virtual ~ServerBase() {}

		class Response : public std::ostream {
			friend class ServerBase<socket_type>;

			asio::streambuf streambuf;

			std::shared_ptr<socket_type> socket;

			Response(std::shared_ptr<socket_type> socket) : std::ostream(&streambuf), socket(socket) {}

		public:
			size_t size() {
				return streambuf.size();
			}
		};

		class Content : public std::istream {
			friend class ServerBase<socket_type>;
		public:
			size_t size() {
				return streambuf.size();
			}
			std::string string() {
				std::stringstream ss;
				ss << rdbuf();
				return ss.str();
			}
		private:
			asio::streambuf &streambuf;
			Content(asio::streambuf &streambuf) : std::istream(&streambuf), streambuf(streambuf) {}
		};

		class Request {
			friend class ServerBase<socket_type>;

			class iequal_to {
			public:
				bool operator()(const std::string &key1, const std::string &key2) const {
					return key1 == key2;
				}
			};
			class ihash {
			public:
				size_t operator()(const std::string &key) const {
					std::size_t seed = 0;
					for (auto &c : key)
						seed ^= (uint8_t)std::tolower(c) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
					return seed;
				}
			};
		public:
			std::string method, path, http_version;

			Content content;

			std::unordered_multimap<std::string, std::string, ihash, iequal_to> header;

			std::smatch path_match;

			std::string remote_endpoint_address;
			unsigned short remote_endpoint_port;

		private:
			Request() : content(streambuf) {}

			asio::streambuf streambuf;

			void read_remote_endpoint_data(socket_type& socket) {
				try {
					remote_endpoint_address = socket.lowest_layer().remote_endpoint().address().to_string();
					remote_endpoint_port = socket.lowest_layer().remote_endpoint().port();
				}
				catch (const std::exception&) {}
			}
		};

		class Config {
			friend class ServerBase<socket_type>;

			Config(unsigned short port, size_t num_threads) : num_threads(num_threads), port(port), reuse_address(true) {}
			size_t num_threads;
		public:
			unsigned short port;
			///IPv4 address in dotted decimal form or IPv6 address in hexadecimal notation.
			///If empty, the address will be any address.
			std::string address;
			///Set to false to avoid binding the socket to an address that is already in use.
			bool reuse_address;
		};
		///Set before calling start().
		Config config;

		std::unordered_map<std::string, std::unordered_map<std::string,
			std::function<void(std::shared_ptr<typename ServerBase<socket_type>::Response>, std::shared_ptr<typename ServerBase<socket_type>::Request>)> > >  resource;

		std::unordered_map<std::string,
			std::function<void(std::shared_ptr<typename ServerBase<socket_type>::Response>, std::shared_ptr<typename ServerBase<socket_type>::Request>)> > default_resource;

	private:
		std::vector<std::pair<std::string, std::vector<std::pair<std::regex,
			std::function<void(std::shared_ptr<typename ServerBase<socket_type>::Response>, std::shared_ptr<typename ServerBase<socket_type>::Request>)> > > > > opt_resource;

	public:
		void start() {
			//Copy the resources to opt_resource for more efficient request processing
			opt_resource.clear();
			for (auto& res : resource) {
				for (auto& res_method : res.second) {
					auto it = opt_resource.end();
					for (auto opt_it = opt_resource.begin();opt_it != opt_resource.end();opt_it++) {
						if (res_method.first == opt_it->first) {
							it = opt_it;
							break;
						}
					}
					if (it == opt_resource.end()) {
						opt_resource.emplace_back();
						it = opt_resource.begin() + (opt_resource.size() - 1);
						it->first = res_method.first;
					}
					it->second.emplace_back(std::regex(res.first), res_method.second);
				}
			}

			if (io_service.stopped())
				io_service.reset();

			asio::ip::tcp::endpoint endpoint;
			if (config.address.size()>0)
				endpoint = asio::ip::tcp::endpoint(asio::ip::address::from_string(config.address), config.port);
			else
				endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), config.port);
			acceptor.open(endpoint.protocol());
			acceptor.set_option(asio::socket_base::reuse_address(config.reuse_address));
			acceptor.bind(endpoint);
			acceptor.listen();

			accept();

			//If num_threads>1, start m_io_service.run() in (num_threads-1) threads for thread-pooling
			threads.clear();
			for (size_t c = 1;c<config.num_threads;c++) {
				threads.emplace_back([this]() {
					io_service.run();
				});
			}

			//Main thread
			io_service.run();

			//Wait for the rest of the threads, if any, to finish as well
			for (auto& t : threads) {
				t.join();
			}
		}

		void stop() {
			acceptor.close();
			io_service.stop();
		}

		///Use this function if you need to recursively send parts of a longer message
		void send(std::shared_ptr<Response> response, const std::function<void(const asio::error_code&)>& callback = nullptr) const {
			asio::async_write(*response->socket, response->streambuf, [this, response, callback](const asio::error_code& ec, size_t /*bytes_transferred*/) {
				if (callback)
					callback(ec);
			});
		}

	protected:
		asio::io_service io_service;
		asio::ip::tcp::acceptor acceptor;
		std::vector<std::thread> threads;

		long timeout_request;
		long timeout_content;

		ServerBase(unsigned short port, size_t num_threads, long timeout_request, long timeout_send_or_receive) :
			config(port, num_threads), acceptor(io_service),
			timeout_request(timeout_request), timeout_content(timeout_send_or_receive) {}

		virtual void accept() = 0;

		std::shared_ptr<asio::steady_timer> set_timeout_on_socket(std::shared_ptr<socket_type> socket, long seconds) {
			std::shared_ptr<asio::steady_timer> timer(new asio::steady_timer(io_service));
			timer->expires_from_now(std::chrono::seconds(seconds));
			timer->async_wait([socket](const asio::error_code& ec) {
				if (!ec) {
					asio::error_code ec;
					socket->lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
					socket->lowest_layer().close();
				}
			});
			return timer;
		}

		void read_request_and_content(std::shared_ptr<socket_type> socket) {
			//Create new streambuf (Request::streambuf) for async_read_until()
			//shared_ptr is used to pass temporary objects to the asynchronous functions
			std::shared_ptr<Request> request(new Request());
			request->read_remote_endpoint_data(*socket);

			//Set timeout on the following asio::async-read or write function
			std::shared_ptr<asio::steady_timer> timer;
			if (timeout_request>0)
				timer = set_timeout_on_socket(socket, timeout_request);

			asio::async_read_until(*socket, request->streambuf, "\r\n\r\n",
				[this, socket, request, timer](const asio::error_code& ec, size_t bytes_transferred) {
				if (timeout_request>0)
					timer->cancel();
				if (!ec) {
					//request->streambuf.size() is not necessarily the same as bytes_transferred, from Boost-docs:
					//"After a successful async_read_until operation, the streambuf may contain additional data beyond the delimiter"
					//The chosen solution is to extract lines from the stream directly when parsing the header. What is left of the
					//streambuf (maybe some bytes of the content) is appended to in the async_read-function below (for retrieving content).
					size_t num_additional_bytes = request->streambuf.size() - bytes_transferred;

					if (!parse_request(request, request->content))
						return;

					//If content, read that as well
					auto it = request->header.find("Content-Length");
					if (it != request->header.end()) {
						//Set timeout on the following asio::async-read or write function
						std::shared_ptr<asio::steady_timer> timer;
						if (timeout_content>0)
							timer = set_timeout_on_socket(socket, timeout_content);
						unsigned long long content_length;
						try {
							content_length = stoull(it->second);
						}
						catch (const std::exception &) {
							return;
						}
						if (content_length>num_additional_bytes) {
							asio::async_read(*socket, request->streambuf,
								asio::transfer_exactly(content_length - num_additional_bytes),
								[this, socket, request, timer]
							(const asio::error_code& ec, size_t /*bytes_transferred*/) {
								if (timeout_content>0)
									timer->cancel();
								if (!ec)
									find_resource(socket, request);
							});
						}
						else {
							if (timeout_content>0)
								timer->cancel();
							find_resource(socket, request);
						}
					}
					else {
						find_resource(socket, request);
					}
				}
			});
		}

		bool parse_request(std::shared_ptr<Request> request, std::istream& stream) const {
			std::string line;
			getline(stream, line);
			size_t method_end;
			if ((method_end = line.find(' ')) != std::string::npos) {
				size_t path_end;
				if ((path_end = line.find(' ', method_end + 1)) != std::string::npos) {
					request->method = line.substr(0, method_end);
					request->path = line.substr(method_end + 1, path_end - method_end - 1);

					size_t protocol_end;
					if ((protocol_end = line.find('/', path_end + 1)) != std::string::npos) {
						if (line.substr(path_end + 1, protocol_end - path_end - 1) != "HTTP")
							return false;
						request->http_version = line.substr(protocol_end + 1, line.size() - protocol_end - 2);
					}
					else
						return false;

					getline(stream, line);
					size_t param_end;
					while ((param_end = line.find(':')) != std::string::npos) {
						size_t value_start = param_end + 1;
						if ((value_start)<line.size()) {
							if (line[value_start] == ' ')
								value_start++;
							if (value_start<line.size())
								request->header.insert(std::make_pair(line.substr(0, param_end), line.substr(value_start, line.size() - value_start - 1)));
						}

						getline(stream, line);
					}
				}
				else
					return false;
			}
			else
				return false;
			return true;
		}

		void find_resource(std::shared_ptr<socket_type> socket, std::shared_ptr<Request> request) {
			//Find path- and method-match, and call write_response
			for (auto& res : opt_resource) {
				if (request->method == res.first) {
					for (auto& res_path : res.second) {
						std::smatch sm_res;
						if (std::regex_match(request->path, sm_res, res_path.first)) {
							request->path_match = std::move(sm_res);
							write_response(socket, request, res_path.second);
							return;
						}
					}
				}
			}
			auto it_method = default_resource.find(request->method);
			if (it_method != default_resource.end()) {
				write_response(socket, request, it_method->second);
			}
		}

		void write_response(std::shared_ptr<socket_type> socket, std::shared_ptr<Request> request,
			std::function<void(std::shared_ptr<typename ServerBase<socket_type>::Response>,
				std::shared_ptr<typename ServerBase<socket_type>::Request>)>& resource_function) {
			//Set timeout on the following asio::async-read or write function
			std::shared_ptr<asio::steady_timer> timer;
			if (timeout_content>0)
				timer = set_timeout_on_socket(socket, timeout_content);

			auto response = std::shared_ptr<Response>(new Response(socket), [this, request, timer](Response *response_ptr) {
				auto response = std::shared_ptr<Response>(response_ptr);
				send(response, [this, response, request, timer](const asio::error_code& ec) {
					if (!ec) {
						if (timeout_content>0)
							timer->cancel();
						float http_version;
						try {
							http_version = stof(request->http_version);
						}
						catch (const std::exception &) {
							return;
						}

						auto range = request->header.equal_range("Connection");
						for (auto it = range.first;it != range.second;it++) {
							if (it->second == "close")
								return;
						}
						if (http_version>1.05)
							read_request_and_content(response->socket);
					}
				});
			});

			try {
				resource_function(response, request);
			}
			catch (const std::exception&) {
				return;
			}
		}
	};

	template<class socket_type>
	class Server : public ServerBase<socket_type> {};

	typedef asio::ip::tcp::socket HTTP;

	template<>
	class Server<HTTP> : public ServerBase<HTTP> {
	public:
		Server(unsigned short port, size_t num_threads = 1, long timeout_request = 5, long timeout_content = 300) :
			ServerBase<HTTP>::ServerBase(port, num_threads, timeout_request, timeout_content) {}

	private:
		void accept() {
			//Create new socket for this connection
			//Shared_ptr is used to pass temporary objects to the asynchronous functions
			std::shared_ptr<HTTP> socket(new HTTP(io_service));

			acceptor.async_accept(*socket, [this, socket](const asio::error_code& ec) {
				//Immediately start accepting a new connection
				accept();

				if (!ec) {
					asio::ip::tcp::no_delay option(true);
					socket->set_option(option);

					read_request_and_content(socket);
				}
			});
		}
	};

	typedef Server<HTTP> HttpServer;
}

namespace tengine
{
	constexpr int WebServer::WEBSERVER_KEY;

	WebServer::WebServer(Service* s)
		: ServiceProxy(s)
		, port_(0)
		, server_(nullptr)
		, worker_(nullptr)
	{

	}

	WebServer::~WebServer()
	{
		if (server_)
		{
			static_cast<HttpServer*>(server_)->stop();

			worker_->join();

			delete static_cast<HttpServer*>(server_);
			server_ = nullptr;

			delete worker_;
			worker_ = nullptr;
		}
	}

	int WebServer::start(uint16_t port, Handler handler)
	{
		port_ = port;

		server_ = new HttpServer(port_, 2);

		if (server_ == nullptr)
			return 1;

		HttpServer *server = static_cast<HttpServer*>(server_);

		server->default_resource["GET"] =
			[=](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{
			auto path = request->path;
			auto content = request->content.string();

			asio::post(this->host()->executor(),
				[=]
			{
				const char* result = handler("GET", path.c_str(), content.c_str());

				if (result)
					*response << "HTTP/1.1 200 OK\r\nContent-Length: " << strlen(result) << "\r\n\r\n" << result;
				else
					//TODO
					*response << "HTTP/1.1 200 OK\r\nContent-Length: " << 0 << "\r\n\r\n" << "";
					;
			});

		};

		server->default_resource["POST"] =
			[=](std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
		{
			auto path = request->path;
			auto content = request->content.string();

			asio::post(this->host()->executor(),
			[=]
			{
				const char* r = handler("POST", path.c_str(), content.c_str());
				if (r)
					*response << "HTTP/1.1 200 OK\r\nContent-Length: " << ::strlen(r) << "\r\n\r\n" << r;

				*response << "HTTP/1.1 200 OK\r\nContent-Length: " << 0 << "\r\n\r\n" << "";
			});

		};

		worker_ = new std::thread([&server]
		{
			server->start();
		});

		return 0;
	}
}
