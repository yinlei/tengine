#include "network.hpp"

#include "context.hpp"
#include "executor.hpp"
#include "sandbox.hpp"
#include "dispatch.hpp"

#include "crypto.hpp"

#include "asio/use_future.hpp"
#include "asio/steady_timer.hpp"
#include "asio/deadline_timer.hpp"
#include "asio/ts/executor.hpp"

#include <iostream>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <random>
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
			if (timeout_content > 0)
				timer = set_timeout_on_socket(socket, timeout_content);

			auto response = std::shared_ptr<Response>(new Response(socket), [this, request, timer](Response *response_ptr) {
				auto response = std::shared_ptr<Response>(response_ptr);
				send(response, [this, response, request, timer](const asio::error_code& ec) {
					if (!ec) {
						if (timeout_content > 0)
							timer->cancel();
						float http_version;
						try {
							http_version = stof(request->http_version);
						}
						catch (const std::exception &) {
							return;
						}

						auto range = request->header.equal_range("Connection");
						for (auto it = range.first; it != range.second; it++) {
							if (it->second == "close")
								return;
						}
						if (http_version > 1.05)
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
					*response << "HTTP/1.1 200 OK\r\n"
					<< "Content-Type: application/json\r\n"
					<< "Access-Control-Allow-Origin: *\r\n"
					<< "Content-Length: " << strlen(result) << "\r\n\r\n"
					<< result;
				else
					//TODO
					*response << "HTTP/1.1 200 OK\r\nContent-Length: " << 0 << "\r\n\r\n" << "";
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
				const char* result = handler("POST", path.c_str(), content.c_str());

				if (result)
					*response << "HTTP/1.1 200 OK\r\n"
					<< "Content-Type: application/json\r\n"
					<< "Access-Control-Allow-Origin: *\r\n"
					<< "Content-Length: " << strlen(result) << "\r\n\r\n"
					<< result;
				else
					//TODO
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

namespace
{
	class case_insensitive_equals {
	public:
		bool operator()(const std::string &key1, const std::string &key2) const {
			return key1 == key2;
		}
	};
	class case_insensitive_hash {
	public:
		size_t operator()(const std::string &key) const {
			std::size_t seed = 0;
			for (auto &c : key)
				seed ^= (uint8_t)std::tolower(c) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			return seed;
		}
	};

	template <class socket_type>
	class SocketServer;

	template <class socket_type>
	class SocketServerBase {
	public:
		virtual ~SocketServerBase() {}

		class SendStream : public std::ostream {
			friend class SocketServerBase<socket_type>;
		private:
			asio::streambuf streambuf;
		public:
			SendStream() : std::ostream(&streambuf) {}
			size_t size() {
				return streambuf.size();
			}
		};

		class Connection {
			friend class SocketServerBase<socket_type>;
			friend class SocketServer<socket_type>;

		public:
			Connection(const std::shared_ptr<socket_type> &socket) : socket(socket), strand(socket->get_io_service()), closed(false) {}

			std::string method, path, http_version;

			std::unordered_multimap<std::string, std::string, case_insensitive_hash, case_insensitive_equals> header;

			std::smatch path_match;

			std::string remote_endpoint_address;
			unsigned short remote_endpoint_port;

		private:
			Connection(socket_type *socket) : socket(socket), strand(socket->get_io_service()), closed(false) {}

			class SendData {
			public:
				SendData(const std::shared_ptr<SendStream> &header_stream, const std::shared_ptr<SendStream> &message_stream,
					const std::function<void(const asio::error_code)> &callback) :
					header_stream(header_stream), message_stream(message_stream), callback(callback) {}
				std::shared_ptr<SendStream> header_stream;
				std::shared_ptr<SendStream> message_stream;
				std::function<void(const asio::error_code)> callback;
			};

			std::shared_ptr<socket_type> socket;

			asio::io_service::strand strand;

			std::list<SendData> send_queue;

			void send_from_queue(const std::shared_ptr<Connection> &connection) {
				strand.post([this, connection]() {
					asio::async_write(*socket, send_queue.begin()->header_stream->streambuf,
						strand.wrap([this, connection](const asio::error_code& ec, size_t /*bytes_transferred*/) {
						if (!ec) {
							asio::async_write(*socket, send_queue.begin()->message_stream->streambuf,
								strand.wrap([this, connection]
								(const asio::error_code& ec, size_t /*bytes_transferred*/) {
								auto send_queued = send_queue.begin();
								if (send_queued->callback)
									send_queued->callback(ec);
								if (!ec) {
									send_queue.erase(send_queued);
									if (send_queue.size()>0)
										send_from_queue(connection);
								}
								else
									send_queue.clear();
							}));
						}
						else {
							auto send_queued = send_queue.begin();
							if (send_queued->callback)
								send_queued->callback(ec);
							send_queue.clear();
						}
					}));
				});
			}

			std::atomic<bool> closed;

			std::unique_ptr<asio::steady_timer> timer_idle;

			void read_remote_endpoint_data() {
				try {
					remote_endpoint_address = socket->lowest_layer().remote_endpoint().address().to_string();
					remote_endpoint_port = socket->lowest_layer().remote_endpoint().port();
				}
				catch (...) {}
			}
		};

		class Message : public std::istream {
			friend class SocketServerBase<socket_type>;

		public:
			unsigned char fin_rsv_opcode;
			size_t size() {
				return length;
			}
			std::string string() {
				std::stringstream ss;
				ss << rdbuf();
				return ss.str();
			}
		private:
			Message() : std::istream(&streambuf) {}
			size_t length;
			asio::streambuf streambuf;
		};

		class Endpoint {
			friend class SocketServerBase<socket_type>;
		private:
			std::unordered_set<std::shared_ptr<Connection> > connections;
			std::mutex connections_mutex;

		public:
			std::function<void(std::shared_ptr<Connection>)> on_open;
			std::function<void(std::shared_ptr<Connection>, std::shared_ptr<Message>)> on_message;
			std::function<void(std::shared_ptr<Connection>, int, const std::string&)> on_close;
			std::function<void(std::shared_ptr<Connection>, const asio::error_code&)> on_error;

			std::unordered_set<std::shared_ptr<Connection> > get_connections() {
				std::lock_guard<std::mutex> lock(connections_mutex);
				auto copy = connections;
				return copy;
			}
		};

		class Config {
			friend class SocketServerBase<socket_type>;
		private:
			Config(unsigned short port) : port(port) {}
		public:
			/// Port number to use. Defaults to 80 for HTTP and 443 for HTTPS.
			unsigned short port;
			/// Number of threads that the server will use when start() is called. Defaults to 1 thread.
			size_t thread_pool_size = 1;
			/// Timeout on request handling. Defaults to 5 seconds.
			size_t timeout_request = 5;
			/// Idle timeout. Defaults to no timeout.
			size_t timeout_idle = 0;
			/// IPv4 address in dotted decimal form or IPv6 address in hexadecimal notation.
			/// If empty, the address will be any address.
			std::string address;
			/// Set to false to avoid binding the socket to an address that is already in use. Defaults to true.
			bool reuse_address = true;
		};
		///Set before calling start().
		Config config;

	private:
		class regex_orderable : public std::regex {
			std::string str;
		public:
			regex_orderable(const char *regex_cstr) : std::regex(regex_cstr), str(regex_cstr) {}
			regex_orderable(const std::string &regex_str) : std::regex(regex_str), str(regex_str) {}
			bool operator<(const regex_orderable &rhs) const {
				return str<rhs.str;
			}
		};
	public:
		/// Warning: do not add or remove endpoints after start() is called
		std::map<regex_orderable, Endpoint> endpoint;

		virtual void start() {
			if (!io_service)
				io_service = std::make_shared<asio::io_service>();

			if (io_service->stopped())
				io_service->reset();

			asio::ip::tcp::endpoint endpoint;
			if (config.address.size()>0)
				endpoint = asio::ip::tcp::endpoint(asio::ip::address::from_string(config.address), config.port);
			else
				endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), config.port);

			if (!acceptor)
				acceptor = std::unique_ptr<asio::ip::tcp::acceptor>(new asio::ip::tcp::acceptor(*io_service));
			acceptor->open(endpoint.protocol());
			acceptor->set_option(asio::socket_base::reuse_address(config.reuse_address));
			acceptor->bind(endpoint);
			acceptor->listen();

			accept();

			//If thread_pool_size>1, start m_io_service.run() in (thread_pool_size-1) threads for thread-pooling
			threads.clear();
			for (size_t c = 1; c<config.thread_pool_size; c++) {
				threads.emplace_back([this]() {
					io_service->run();
				});
			}
			//Main thread
			if (config.thread_pool_size>0)
				io_service->run();

			//Wait for the rest of the threads, if any, to finish as well
			for (auto& t : threads) {
				t.join();
			}
		}

		void stop() {
			acceptor->close();
			if (config.thread_pool_size>0)
				io_service->stop();

			for (auto& p : endpoint)
				p.second.connections.clear();
		}

		///fin_rsv_opcode: 129=one fragment, text, 130=one fragment, binary, 136=close connection.
		///See http://tools.ietf.org/html/rfc6455#section-5.2 for more information
		void send(const std::shared_ptr<Connection> &connection, const std::shared_ptr<SendStream> &message_stream,
			const std::function<void(const asio::error_code&)>& callback = nullptr,
			unsigned char fin_rsv_opcode = 129) const {
			if (fin_rsv_opcode != 136)
				timer_idle_reset(connection);

			auto header_stream = std::make_shared<SendStream>();

			size_t length = message_stream->size();

			header_stream->put(fin_rsv_opcode);
			//unmasked (first length byte<128)
			if (length >= 126) {
				int num_bytes;
				if (length>0xffff) {
					num_bytes = 8;
					header_stream->put(127);
				}
				else {
					num_bytes = 2;
					header_stream->put(126);
				}

				for (int c = num_bytes - 1; c >= 0; c--) {
					header_stream->put((static_cast<unsigned long long>(length) >> (8 * c)) % 256);
				}
			}
			else
				header_stream->put(static_cast<unsigned char>(length));

			connection->strand.post([this, connection, header_stream, message_stream, callback]() {
				connection->send_queue.emplace_back(header_stream, message_stream, callback);
				if (connection->send_queue.size() == 1)
					connection->send_from_queue(connection);
			});
		}

		void send_close(const std::shared_ptr<Connection> &connection, int status, const std::string& reason = "",
			const std::function<void(const asio::error_code&)>& callback = nullptr) const {
			//Send close only once (in case close is initiated by server)
			if (connection->closed)
				return;
			connection->closed = true;

			auto send_stream = std::make_shared<SendStream>();

			send_stream->put(status >> 8);
			send_stream->put(status % 256);

			*send_stream << reason;

			//fin_rsv_opcode=136: message close
			send(connection, send_stream, callback, 136);
		}

		std::unordered_set<std::shared_ptr<Connection> > get_connections() {
			std::unordered_set<std::shared_ptr<Connection> > all_connections;
			for (auto& e : endpoint) {
				std::lock_guard<std::mutex> lock(e.second.connections_mutex);
				all_connections.insert(e.second.connections.begin(), e.second.connections.end());
			}
			return all_connections;
		}

		/**
		* Upgrades a request, from for instance Simple-Web-Server, to a WebSocket connection.
		* The parameters are moved to the Connection object.
		* See also Server::on_upgrade in the Simple-Web-Server project.
		* The socket's io_service is used, thus running start() is not needed.
		*
		* Example use:
		* server.on_upgrade=[&socket_server] (auto socket, auto request) {
		*   auto connection=std::make_shared<SimpleWeb::SocketServer<SimpleWeb::WS>::Connection>(socket);
		*   connection->method=std::move(request->method);
		*   connection->path=std::move(request->path);
		*   connection->http_version=std::move(request->http_version);
		*   connection->header=std::move(request->header);
		*   connection->remote_endpoint_address=std::move(request->remote_endpoint_address);
		*   connection->remote_endpoint_port=request->remote_endpoint_port;
		*   socket_server.upgrade(connection);
		* }
		*/
		void upgrade(const std::shared_ptr<Connection> &connection) {
			auto read_buffer = std::make_shared<asio::streambuf>();
			write_handshake(connection, read_buffer);
		}

		/// If you have your own asio::io_service, store its pointer here before running start().
		/// You might also want to set config.thread_pool_size to 0.
		std::shared_ptr<asio::io_service> io_service;
	protected:
		const std::string ws_magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

		std::unique_ptr<asio::ip::tcp::acceptor> acceptor;

		std::vector<std::thread> threads;

		SocketServerBase(unsigned short port) : config(port) {}

		virtual void accept() = 0;

		std::shared_ptr<asio::steady_timer> get_timeout_timer(const std::shared_ptr<Connection> &connection, size_t seconds) {
			if (seconds == 0)
				return nullptr;

			auto timer = std::make_shared<asio::steady_timer>(connection->socket->get_io_service());
			timer->expires_from_now(std::chrono::seconds(static_cast<long>(seconds)));
			timer->async_wait([connection](const asio::error_code& ec) {
				if (!ec) {
					connection->socket->lowest_layer().shutdown(asio::ip::tcp::socket::shutdown_both);
					connection->socket->lowest_layer().close();
				}
			});
			return timer;
		}

		void read_handshake(const std::shared_ptr<Connection> &connection) {
			connection->read_remote_endpoint_data();

			//Create new read_buffer for async_read_until()
			//Shared_ptr is used to pass temporary objects to the asynchronous functions
			auto read_buffer = std::make_shared<asio::streambuf>();

			//Set timeout on the following asio::async-read or write function
			auto timer = get_timeout_timer(connection, config.timeout_request);

			asio::async_read_until(*connection->socket, *read_buffer, "\r\n\r\n",
				[this, connection, read_buffer, timer]
			(const asio::error_code& ec, size_t /*bytes_transferred*/) {
				if (timer)
					timer->cancel();
				if (!ec) {
					//Convert to istream to extract string-lines
					std::istream stream(read_buffer.get());

					parse_handshake(connection, stream);

					write_handshake(connection, read_buffer);
				}
			});
		}

		void parse_handshake(const std::shared_ptr<Connection> &connection, std::istream& stream) const {
			std::string line;
			getline(stream, line);
			size_t method_end;
			if ((method_end = line.find(' ')) != std::string::npos) {
				size_t path_end;
				if ((path_end = line.find(' ', method_end + 1)) != std::string::npos) {
					connection->method = line.substr(0, method_end);
					connection->path = line.substr(method_end + 1, path_end - method_end - 1);
					if ((path_end + 6)<line.size())
						connection->http_version = line.substr(path_end + 6, line.size() - (path_end + 6) - 1);
					else
						connection->http_version = "1.1";

					getline(stream, line);
					size_t param_end;
					while ((param_end = line.find(':')) != std::string::npos) {
						size_t value_start = param_end + 1;
						if ((value_start)<line.size()) {
							if (line[value_start] == ' ')
								value_start++;
							if (value_start<line.size())
								connection->header.emplace(line.substr(0, param_end), line.substr(value_start, line.size() - value_start - 1));
						}

						getline(stream, line);
					}
				}
			}
		}

		void write_handshake(const std::shared_ptr<Connection> &connection, const std::shared_ptr<asio::streambuf> &read_buffer) {
			//Find path- and method-match, and generate response
			for (auto &regex_endpoint : endpoint) {
				std::smatch path_match;
				if (std::regex_match(connection->path, path_match, regex_endpoint.first)) {
					auto write_buffer = std::make_shared<asio::streambuf>();
					std::ostream handshake(write_buffer.get());

					if (generate_handshake(connection, handshake)) {
						connection->path_match = std::move(path_match);
						//Capture write_buffer in lambda so it is not destroyed before async_write is finished
						asio::async_write(*connection->socket, *write_buffer,
							[this, connection, write_buffer, read_buffer, &regex_endpoint]
						(const asio::error_code& ec, size_t /*bytes_transferred*/) {
							if (!ec) {
								connection_open(connection, regex_endpoint.second);
								read_message(connection, read_buffer, regex_endpoint.second);
							}
							else
								connection_error(connection, regex_endpoint.second, ec);
						});
					}
					return;
				}
			}
		}

		bool generate_handshake(const std::shared_ptr<Connection> &connection, std::ostream& handshake) const {
			auto header_it = connection->header.find("Sec-WebSocket-Key");
			if (header_it == connection->header.end())
				return false;

			auto sha1 = tengine::Crypto::sha1(header_it->second + ws_magic_string);

			handshake << "HTTP/1.1 101 Web Socket Protocol Handshake\r\n";
			handshake << "Upgrade: websocket\r\n";
			handshake << "Connection: Upgrade\r\n";
			handshake << "Sec-WebSocket-Accept: " << tengine::Crypto::Base64::encode(sha1) << "\r\n";
			handshake << "\r\n";

			return true;
		}

		void read_message(const std::shared_ptr<Connection> &connection,
			const std::shared_ptr<asio::streambuf> &read_buffer, Endpoint& endpoint) const {
			asio::async_read(*connection->socket, *read_buffer, asio::transfer_exactly(2),
				[this, connection, read_buffer, &endpoint]
			(const asio::error_code& ec, size_t bytes_transferred) {
				if (!ec) {
					if (bytes_transferred == 0) { //TODO: why does this happen sometimes?
						read_message(connection, read_buffer, endpoint);
						return;
					}
					std::istream stream(read_buffer.get());

					std::vector<unsigned char> first_bytes;
					first_bytes.resize(2);
					stream.read((char*)&first_bytes[0], 2);

					unsigned char fin_rsv_opcode = first_bytes[0];

					//Close connection if unmasked message from client (protocol error)
					if (first_bytes[1]<128) {
						const std::string reason("message from client not masked");
						send_close(connection, 1002, reason, [this, connection](const asio::error_code& /*ec*/) {});
						connection_close(connection, endpoint, 1002, reason);
						return;
					}

					size_t length = (first_bytes[1] & 127);

					if (length == 126) {
						//2 next bytes is the size of content
						asio::async_read(*connection->socket, *read_buffer, asio::transfer_exactly(2),
							[this, connection, read_buffer, &endpoint, fin_rsv_opcode]
						(const asio::error_code& ec, size_t /*bytes_transferred*/) {
							if (!ec) {
								std::istream stream(read_buffer.get());

								std::vector<unsigned char> length_bytes;
								length_bytes.resize(2);
								stream.read((char*)&length_bytes[0], 2);

								size_t length = 0;
								int num_bytes = 2;
								for (int c = 0; c<num_bytes; c++)
									length += length_bytes[c] << (8 * (num_bytes - 1 - c));

								read_message_content(connection, read_buffer, length, endpoint, fin_rsv_opcode);
							}
							else
								connection_error(connection, endpoint, ec);
						});
					}
					else if (length == 127) {
						//8 next bytes is the size of content
						asio::async_read(*connection->socket, *read_buffer, asio::transfer_exactly(8),
							[this, connection, read_buffer, &endpoint, fin_rsv_opcode]
						(const asio::error_code& ec, size_t /*bytes_transferred*/) {
							if (!ec) {
								std::istream stream(read_buffer.get());

								std::vector<unsigned char> length_bytes;
								length_bytes.resize(8);
								stream.read((char*)&length_bytes[0], 8);

								size_t length = 0;
								int num_bytes = 8;
								for (int c = 0; c<num_bytes; c++)
									length += length_bytes[c] << (8 * (num_bytes - 1 - c));

								read_message_content(connection, read_buffer, length, endpoint, fin_rsv_opcode);
							}
							else
								connection_error(connection, endpoint, ec);
						});
					}
					else
						read_message_content(connection, read_buffer, length, endpoint, fin_rsv_opcode);
				}
				else
					connection_error(connection, endpoint, ec);
			});
		}

		void read_message_content(const std::shared_ptr<Connection> &connection, const std::shared_ptr<asio::streambuf> &read_buffer,
			size_t length, Endpoint& endpoint, unsigned char fin_rsv_opcode) const {
			asio::async_read(*connection->socket, *read_buffer, asio::transfer_exactly(4 + length),
				[this, connection, read_buffer, length, &endpoint, fin_rsv_opcode]
			(const asio::error_code& ec, size_t /*bytes_transferred*/) {
				if (!ec) {
					std::istream raw_message_data(read_buffer.get());

					//Read mask
					std::vector<unsigned char> mask;
					mask.resize(4);
					raw_message_data.read((char*)&mask[0], 4);

					std::shared_ptr<Message> message(new Message());
					message->length = length;
					message->fin_rsv_opcode = fin_rsv_opcode;

					std::ostream message_data_out_stream(&message->streambuf);
					for (size_t c = 0; c<length; c++) {
						message_data_out_stream.put(raw_message_data.get() ^ mask[c % 4]);
					}

					//If connection close
					if ((fin_rsv_opcode & 0x0f) == 8) {
						int status = 0;
						if (length >= 2) {
							unsigned char byte1 = message->get();
							unsigned char byte2 = message->get();
							status = (byte1 << 8) + byte2;
						}

						auto reason = message->string();
						send_close(connection, status, reason, [this, connection](const asio::error_code& /*ec*/) {});
						connection_close(connection, endpoint, status, reason);
						return;
					}
					else {
						//If ping
						if ((fin_rsv_opcode & 0x0f) == 9) {
							//send pong
							auto empty_send_stream = std::make_shared<SendStream>();
							send(connection, empty_send_stream, nullptr, fin_rsv_opcode + 1);
						}
						else if (endpoint.on_message) {
							timer_idle_reset(connection);
							endpoint.on_message(connection, message);
						}

						//Next message
						read_message(connection, read_buffer, endpoint);
					}
				}
				else
					connection_error(connection, endpoint, ec);
			});
		}

		void connection_open(const std::shared_ptr<Connection> &connection, Endpoint& endpoint) {
			timer_idle_init(connection);

			{
				std::lock_guard<std::mutex> lock(endpoint.connections_mutex);
				endpoint.connections.insert(connection);
			}

			if (endpoint.on_open)
				endpoint.on_open(connection);
		}

		void connection_close(const std::shared_ptr<Connection> &connection, Endpoint& endpoint, int status, const std::string& reason) const {
			timer_idle_cancel(connection);

			{
				std::lock_guard<std::mutex> lock(endpoint.connections_mutex);
				endpoint.connections.erase(connection);
			}

			if (endpoint.on_close)
				endpoint.on_close(connection, status, reason);
		}

		void connection_error(const std::shared_ptr<Connection> &connection, Endpoint& endpoint, const asio::error_code& ec) const {
			timer_idle_cancel(connection);

			{
				std::lock_guard<std::mutex> lock(endpoint.connections_mutex);
				endpoint.connections.erase(connection);
			}

			if (endpoint.on_error)
				endpoint.on_error(connection, ec);
		}

		void timer_idle_init(const std::shared_ptr<Connection> &connection) {
			if (config.timeout_idle>0) {
				connection->timer_idle = std::unique_ptr<asio::steady_timer>(new asio::steady_timer(connection->socket->get_io_service()));
				connection->timer_idle->expires_from_now(std::chrono::seconds(static_cast<unsigned long>(config.timeout_idle)));
				timer_idle_expired_function(connection);
			}
		}
		void timer_idle_reset(const std::shared_ptr<Connection> &connection) const {
			if (config.timeout_idle>0 && connection->timer_idle->expires_from_now(std::chrono::seconds(static_cast<unsigned long>(config.timeout_idle)))>0)
				timer_idle_expired_function(connection);
		}
		void timer_idle_cancel(const std::shared_ptr<Connection> &connection) const {
			if (config.timeout_idle>0)
				connection->timer_idle->cancel();
		}

		void timer_idle_expired_function(const std::shared_ptr<Connection> &connection) const {
			connection->timer_idle->async_wait([this, connection](const asio::error_code& ec) {
				if (!ec)
					send_close(connection, 1000, "idle timeout"); //1000=normal closure
			});
		}
	};

	template<class socket_type>
	class SocketServer : public SocketServerBase<socket_type> {};

	typedef asio::ip::tcp::socket WS;

	template<>
	class SocketServer<WS> : public SocketServerBase<WS> {
	public:
		SocketServer(unsigned short port, size_t thread_pool_size = 1, size_t timeout_request = 5, size_t timeout_idle = 0) :
			SocketServer() {
			config.port = port;
			config.thread_pool_size = thread_pool_size;
			config.timeout_request = timeout_request;
			config.timeout_idle = timeout_idle;
		};

		SocketServer() : SocketServerBase<WS>(80) {}

	protected:
		void accept() {
			//Create new socket for this connection (stored in Connection::socket)
			//Shared_ptr is used to pass temporary objects to the asynchronous functions
			std::shared_ptr<Connection> connection(new Connection(new WS(*io_service)));

			acceptor->async_accept(*connection->socket, [this, connection](const asio::error_code& ec) {
				//Immediately start accepting a new connection (if io_service hasn't been stopped)
				if (ec != asio::error::operation_aborted)
					accept();

				if (!ec) {
					asio::ip::tcp::no_delay option(true);
					connection->socket->set_option(option);

					read_handshake(connection);
				}
			});
		}
	};

	typedef SocketServer<WS> WebSocketServer;
}

namespace tengine
{
	constexpr int WebSocket::WEBSOCKET_KEY;

	WebSocket::WebSocket(Service *s, uint16_t port)
		: ServiceProxy(s)
		, port_(port)
		, server_(nullptr)
		, worker_(nullptr)
		, session_lock_()
	{

	}

	WebSocket::~WebSocket()
	{
		if (server_)
		{
			static_cast<WebSocketServer*>(server_)->stop();

			worker_->join();

			delete static_cast<WebSocketServer*>(server_);
			server_ = nullptr;

			delete worker_;
			worker_ = nullptr;
		}
	}

	int WebSocket::start(const std::string& path)
	{
		server_ = new WebSocketServer(port_, 2);

		if (server_ == nullptr)
			return 1;

		WebSocketServer *server = static_cast<WebSocketServer*>(server_);

		auto& s = (*server).endpoint["^/" + path +"/?$"];

		s.on_open=[this](std::shared_ptr<WebSocketServer::Connection> connection) {
			
			std::cout << "Server: Opened connection " << (size_t)connection.get() << std::endl;
			dispatch<MessageType::kMessageWebServerOpen, SandBox>(
				this->host(), this->host(), (void*)this, (int)connection.get());
		};

		s.on_message=[this](std::shared_ptr<WebSocketServer::Connection> connection, 
			std::shared_ptr<WebSocketServer::Message> message) {
			auto& message_str = message->string();

			std::size_t size = message_str.size();
			char *msg = (char*)ccmalloc(size);
			memcpy(msg, message_str.c_str(), size);

			dispatch<MessageType::kMessageWebServerMessage, SandBox>(
				this->host(), this->host(), (void*)this, (int)connection.get(), (const char*)msg, size);
		};

		s.on_close=[this](std::shared_ptr<WebSocketServer::Connection> connection, int status, const std::string& reason) {
			std::cout << "Server: Closed connection " << (size_t)connection.get() << " with status code " << status << std::endl;
			dispatch<MessageType::kMessageWebServerClose, SandBox>(
				this->host(), this->host(), (void*)this, (int)connection.get(), status, reason);
    	};

		s.on_error=[this](std::shared_ptr<WebSocketServer::Connection> connection, const asio::error_code& ec) {
			std::cout << "Server: Error in connection " << (size_t)connection.get() << ". " <<
				"Error: " << ec << ", error message: " << ec.message() << std::endl;
			dispatch<MessageType::kMessageWebServerError, SandBox>(
				this->host(), this->host(), (void*)this, (int)connection.get(), ec.message());
		};

		worker_ = new std::thread([&server]
		{
			server->start();
		});

		return 0;
	}

	void WebSocket::send(int session, const char *data, size_t len)
	{
		if (server_ == nullptr)
			return;
		
		WebSocketServer *server = static_cast<WebSocketServer*>(server_);
		for (auto& con : server->get_connections())
		{
			if ((std::size_t)con.get() == session)
			{
				auto send_stream = std::make_shared<WebSocketServer::SendStream>();
       		 	*send_stream << data;
				server->send(con, send_stream);
				return;
			}
		}

	}
}