#include "threadpool/handle_set.hpp"
#include "threadpool/event_handler.hpp"

#include <unistd.h>
#include <stdexcept>

HandleSet::HandleSet(int max_events)
	: epoll_fd_(-1), max_events_count_(max_events), events_(max_events) {
	epoll_fd_ = epoll_create1(0);
	if (epoll_fd_ < 0) {
		throw std::runtime_error("epoll_create1 failed");
	}
}

HandleSet::~HandleSet() {
	if (epoll_fd_ >= 0) {
		close(epoll_fd_);
	}
}

int HandleSet::wait_for_event(int timeout) {
	const int n = epoll_wait(epoll_fd_, events_.data(), max_events_count_, timeout);
	return n;
}

void HandleSet::register_handle(int fd, EventHandler* handler, uint32_t events) {
	if (fd < 0 || handler == nullptr) {
		return;
	}

	epoll_event ev{};
	ev.events = events;
	ev.data.fd = fd;

	// 先尝试 ADD，失败再 MOD（便于重复注册/更新事件掩码）
	if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) != 0) {
		epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
	}

	mutex_.lock();
	handlers_[fd] = handler;
	mutex_.unlock();
}

void HandleSet::unregister_handle(int fd) {
	if (fd < 0) {
		return;
	}

	epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

	mutex_.lock();
	handlers_.erase(fd);
	mutex_.unlock();
}

std::pair<EventHandler*, uint32_t> HandleSet::get_ready_handler(int index) {
	if (index < 0 || index >= static_cast<int>(events_.size())) {
		return {nullptr, 0};
	}

	const int fd = events_[index].data.fd;
	const uint32_t ev = events_[index].events;

	mutex_.lock();
	auto it = handlers_.find(fd);
	EventHandler* handler = (it == handlers_.end()) ? nullptr : it->second;
	mutex_.unlock();

	return {handler, ev};
}
