#include "network.hpp"

#include "context.hpp"
#include "executor.hpp"
#include "sandbox.hpp"
#include "dispatch.hpp"

namespace tengine
{
	HttpClient::HttpClient(Context& context)
		: Service(context)
		, executor_(new Executor())
	{

	}

	HttpClient::~HttpClient()
	{
		delete executor_;
	}

	int HttpClient::init(const char* name)
	{
		Service::init(name);

		context_.register_name(this, "Network");

		executor_->run();

		return 0;
	}

	const std::string HttpClient::do_request(const std::string& type, const std::string& url, const std::string& path, const std::string& content)
	{
		SimpleWeb::HttpClient client(url);

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

	const std::string HttpClient::sync_request(const char *type, const char *url, const char *path, const char* content)
	{
		return do_request(type, url, path, content);
	}

	void HttpClient::async_request(const char *type, const char *url, const char *path, const char* content, Handler handler)
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

namespace tengine
{
	constexpr int HttpServer::HTTPSERVER_KEY;

	HttpServer::HttpServer(Service* s)
		: ServiceProxy(s)
		, port_(0)
		, server_(nullptr)
		, worker_(nullptr)
	{

	}

	HttpServer::~HttpServer()
	{
		if (server_)
		{
			server_->stop();

			worker_->join();

			delete server_;
			server_ = nullptr;

			delete worker_;
			worker_ = nullptr;
		}
	}

	int HttpServer::start(uint16_t port, Handler handler)
	{
		port_ = port;

		server_ = new SimpleWeb::HttpServer(port_, 2);

		if (server_ == nullptr)
			return 1;

		server_->default_resource["GET"] =
			[=](std::shared_ptr<SimpleWeb::HttpServer::Response> response, 
				std::shared_ptr<SimpleWeb::HttpServer::Request> request)
		{
			auto path = request->path;
			auto content = request->content.string();

			{
				std::lock_guard<std::mutex> lock(mutex_);
				responses_.insert(std::make_pair(response.get(), response));
			}

			asio::post(this->host()->executor(),
				[=]
			{
				auto res = response;
				auto req = request;

				handler(res.get(), "GET", path.c_str(), content.c_str());

				/*
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
				*/
			});

		};

		server_->default_resource["POST"] =
			[=](std::shared_ptr<SimpleWeb::HttpServer::Response> response, 
				std::shared_ptr<SimpleWeb::HttpServer::Request> request)
		{
			{
				std::lock_guard<std::mutex> lock(mutex_);
				responses_.insert(std::make_pair(response.get(), response));
			}

			auto path = request->path;
			auto content = request->content.string();

			asio::post(this->host()->executor(),
				[=]
			{
				auto res = response;
				auto req = request;
				
				handler(res.get(), "POST",  path.c_str(), content.c_str());
				/*
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
				*/
			});

		};

		worker_ = new std::thread([this]
		{
			this->server_->start();
		});

		return 0;
	}

	void HttpServer::response(void* response, const char* result, std::size_t size, bool remove)
	{
		/*
		HttpServer::Response *res = static_cast<HttpServer::Response*>(response);
		if (res)
			*res << "HTTP/1.1 200 OK\r\n"
			<< "Content-Type: application/json\r\n"
			<< "Access-Control-Allow-Origin: *\r\n"
			<< "Content-Length: " << size << "\r\n\r\n"
			<< result;
		else
			//TODO
			*res << "HTTP/1.1 200 OK\r\nContent-Length: " << 0 << "\r\n\r\n" << "";
		*/
		if (response && result)
		{
			SimpleWeb::HttpServer::Response *res = static_cast<SimpleWeb::HttpServer::Response*>(response);
			*res << std::string{ result, size };
		}

		if (remove)
		{
			std::lock_guard<std::mutex> lock(mutex_);
			responses_.erase(response);
		}
	}
}

namespace tengine
{
	constexpr int WebServer::WEBSERVER_KEY;

	WebServer::WebServer(Service *s, uint16_t port)
		: ServiceProxy(s)
		, port_(port)
		, server_(nullptr)
		, worker_(nullptr)
		, session_lock_()
	{

	}

	WebServer::~WebServer()
	{
		if (server_)
		{
			server_->stop();

			worker_->join();

			delete server_;
			server_ = nullptr;

			delete worker_;
			worker_ = nullptr;
		}
	}

	int WebServer::start(const std::string& path)
	{
		server_ = new SimpleWeb::WebSocketServer(port_, 2);

		if (server_ == nullptr)
			return 1;

		auto& s = (*server_).endpoint["^/" + path +"/?$"];

		s.on_open=[this](std::shared_ptr<SimpleWeb::WebSocketServer::Connection> connection) {
			
			std::cout << "Server: Opened connection " << (size_t)connection.get() << std::endl;
			dispatch<MessageType::kMessageWebServerOpen, SandBox>(
				this->host(), this->host(), (void*)this, (int)connection.get());
		};

		s.on_message=[this](std::shared_ptr<SimpleWeb::WebSocketServer::Connection> connection,
			std::shared_ptr<SimpleWeb::WebSocketServer::Message> message) {
			auto& message_str = message->string();

			std::size_t size = message_str.size();
			char *msg = (char*)ccmalloc(size);
			memcpy(msg, message_str.c_str(), size);

			dispatch<MessageType::kMessageWebServerMessage, SandBox>(
				this->host(), this->host(), (void*)this, (int)connection.get(), (const char*)msg, size);
		};

		s.on_close=[this](std::shared_ptr<SimpleWeb::WebSocketServer::Connection> connection, int status, const std::string& reason) {
			std::cout << "Server: Closed connection " << (size_t)connection.get() << " with status code " << status << std::endl;
			dispatch<MessageType::kMessageWebServerClose, SandBox>(
				this->host(), this->host(), (void*)this, (int)connection.get(), status, reason);
    	};

		s.on_error=[this](std::shared_ptr<SimpleWeb::WebSocketServer::Connection> connection, const asio::error_code& ec) {
			std::cout << "Server: Error in connection " << (size_t)connection.get() << ". " <<
				"Error: " << ec << ", error message: " << ec.message() << std::endl;
			dispatch<MessageType::kMessageWebServerError, SandBox>(
				this->host(), this->host(), (void*)this, (int)connection.get(), ec.message());
		};

		worker_ = new std::thread([this]
		{
			this->server_->start();
		});

		return 0;
	}

	void WebServer::send(int session, const char *data, size_t len)
	{
		if (server_ == nullptr)
			return;

		for (auto& con : server_->get_connections())
		{
			if ((std::size_t)con.get() == session)
			{
				auto send_stream = std::make_shared<SimpleWeb::WebSocketServer::SendStream>();
       		 	*send_stream << data;
				server_->send(con, send_stream);
				return;
			}
		}

	}
}