#pragma once
#include <cstddef>
#include <functional>

namespace conway::app {
// Completed jobs, not elapsed time. Invoked only outside measured intervals.
using BenchmarkProgressCallback = std::function<void(std::size_t completed,std::size_t total)>;
namespace detail {
class BenchmarkProgressCounter {
public:
    BenchmarkProgressCounter(std::size_t total,const BenchmarkProgressCallback& callback)
        : total_(total),callback_(callback) { if(callback_) callback_(0,total_); }
    void advance() { ++completed_; if(callback_) callback_(completed_,total_); }
private:
    std::size_t total_,completed_{};
    const BenchmarkProgressCallback& callback_;
};
}
}
