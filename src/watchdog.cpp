#include "watchdog.hpp"

#include "context.hpp"
#include "executor.hpp"

#include "dispatch.hpp"

#include <functional>

namespace tengine
{
	static std::pair<fs::path, std::string> get_path_filter_pair(
		const fs::path &path)
	{
		std::string key = path.string();

		fs::path p = path;
		std::size_t wildcard_pos = key.find("*");

		std::string filter;

		if (wildcard_pos != std::string::npos)
		{
			filter = path.filename().string();
			p = path.parent_path();
		}

		if (filter.empty() && !fs::exists(p))
		{
			//throw std::exception(path)
		}

		return std::make_pair(p, filter);
	}

	static std::pair<fs::path, std::string> visit_wildcard_path(
		const fs::path &path,
		const std::function<bool(const fs::path&)> visitor)
	{
		std::pair<fs::path, std::string> path_filter =
			get_path_filter_pair(path);
		if (!path_filter.second.empty())
		{
			std::string full =
				(path_filter.first / path_filter.second).string();
			size_t wildcard_pos = full.find("*");
			std::string before = full.substr(0, wildcard_pos);
			std::string after = full.substr(wildcard_pos + 1);
			fs::directory_iterator end;
			for (fs::directory_iterator it(path_filter.first); it != end; ++it)
			{
				std::string current = it->path().string();
				size_t before_pos = current.find(before);
				size_t after_pos = current.find(after);
				if ((before_pos != std::string::npos || before.empty())
					&& (after_pos != std::string::npos || after.empty()))
				{
					if (visitor(it->path()))
					{
						break;
					}
				}
			}
		}

		return path_filter;
	}

	class Watcher : public Allocator
	{
	public:
		Watcher(WatchDog &dog, int dest,
			const fs::path& path, const std::string &filter)
		: dog_(dog)
		, dest_(dest)
		, path_(path)
		, filter_(filter)
		, modification_times_()
		{
			if (!filter_.empty())
			{
				std::vector<fs::path> paths;

				visit_wildcard_path(path / filter,
					[this, &paths](const fs::path &p)
				{
					changed(p);
					paths.push_back(p);
					return false;
				});

				on_path_changed(path / filter);

				on_dir_changed(paths);
			}
		}

		void watch()
		{
			if (!filter_.empty())
			{
				std::vector<fs::path> paths;

				visit_wildcard_path(path_ / filter_,
					[this, &paths](const fs::path &p)
					{
						bool path_changed = changed(p);
						if (path_changed)
						{
							on_path_changed(path_ / filter_);

							paths.push_back(p);

							return true;
						}

						return false;
					}
				);

				if (paths.size() > 0)
				{
					on_dir_changed(paths);
				}
			}
			else
			{
				if (changed(path_))
				{
					on_path_changed(path_);
				}
			}
		}

		bool changed(const fs::path &path)
		{
			auto time = fs::last_write_time(path);

			auto key = path.string();

			if (modification_times_.find(key) == modification_times_.end())
			{
				modification_times_[key] = time;
				return true;
			}

			auto &prev = modification_times_[key];
			if (prev < time)
			{
				prev = time;
				return true;
			}

			return false;
		}

	private:

		void on_path_changed(const fs::path &path)
		{
			dispatch<SandBox>(
				dog_.context(), dog_.id(), dest_, path, (void*)this);
		}

		void on_dir_changed(const std::vector<fs::path> &paths)
		{
			dispatch<SandBox>(
				dog_.context(), dog_.id(), dest_, paths, (void*)this);
		}

	private:

		WatchDog& dog_;

		int dest_;

		fs::path path_;

		std::string filter_;

		std::map<std::string, fs::file_time_type> modification_times_;
	};

	 constexpr int WatchDog::WATCHDOG_KEY;

	WatchDog::WatchDog(Context& context)
		: Service(context)
		, executor_(new Executor())
		, timer_(executor_->io_service())
		, file_watchers_()
	{

	}

	WatchDog::~WatchDog()
	{
		timer_.cancel();

		delete executor_;
		executor_ = nullptr;

	}

	int WatchDog::init(const char* name)
	{
		Service::init(name);

		char key[256];

		snprintf(key, sizeof(key), "%s.", name);

		context_.register_name(this, "WatchDog");

		executor_->run();

		do_watch();

		return 0;
	}

	void WatchDog::do_watch()
	{
		timer_.expires_from_now(std::chrono::seconds(1));
		timer_.async_wait([this](const asio::error_code& ec) {
			if (!ec) {
				auto end = file_watchers_.end();

				for (auto it = file_watchers_.begin(); it != end; ++it)
				{
					it->second->watch();
				}

				do_watch();
			}
		});
	}

	void* WatchDog::watch(ServiceAddress src, const fs::path &path)
	{
		std::string key = path.string();

		std::string filter;

		fs::path p = path;

		if (path.string().find("*") != std::string::npos)
		{
			bool found = false;

			std::pair<fs::path, std::string> path_filter =
			visit_wildcard_path(path,
				[&found](const fs::path &p) {
				found = true;
				return true;
			});

			if (found)
			{
				p = path_filter.first;
				filter = path_filter.second;
			}
		}

		Watcher *watcher = new Watcher(*this, src->id(), p, filter);

		asio::post(executor_->executor(),
			[=]
		{
			if (file_watchers_.find(key) == file_watchers_.end())
			{
				file_watchers_.emplace(std::make_pair(key, watcher));
			}
		});

		return watcher;
	}

	void WatchDog::unwatch(const fs::path &path)
	{
		std::string key = path.string();

		asio::post(executor_->executor(),
			[=]
		{
			if (path.empty())
			{
				for (auto it = file_watchers_.begin();
					it != file_watchers_.end(); )
				{
					delete it->second;

					it = file_watchers_.erase(it);
				}
			}
			else
			{
				auto watcher = file_watchers_.find(key);
				if (watcher != file_watchers_.end())
				{
					delete watcher->second;

					file_watchers_.erase(watcher);
				}
			}
		});
	}

	void WatchDog::touch(const fs::path &path, fs::file_time_type time)
	{
		if (fs::exists(path))
		{
			fs::last_write_time(path, time);
			return;
		}

		if (path.string().find("*") != std::string::npos)
		{
			visit_wildcard_path(path, [time](const fs::path &p) {
				fs::last_write_time(p, time);
				return false;
			});
		}
	}
}
