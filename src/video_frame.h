#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
struct VideoFrame {unsigned width{},height{};std::vector<std::uint8_t> bgra;std::uint64_t sequence{};};
class VideoFrameMailbox {
    mutable std::mutex mutex_;std::condition_variable changed_;
    std::shared_ptr<const VideoFrame> current_;std::uint64_t sequence_{},generation_{};
public:
    void publish(std::shared_ptr<VideoFrame> frame){
        {std::lock_guard lock(mutex_);frame->sequence=++sequence_;current_=std::move(frame);++generation_;}changed_.notify_all();
    }
    std::shared_ptr<const VideoFrame> snapshot()const{std::lock_guard lock(mutex_);return current_;}
    std::shared_ptr<const VideoFrame> wait(std::uint64_t after,unsigned timeout_ms){
        std::unique_lock lock(mutex_);const auto generation=generation_;
        changed_.wait_for(lock,std::chrono::milliseconds(timeout_ms),[&]{return generation_!=generation||(current_&&current_->sequence!=after);});return current_;
    }
    void clear(){{std::lock_guard lock(mutex_);current_.reset();++generation_;}changed_.notify_all();}
};
