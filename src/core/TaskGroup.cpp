#include "TaskGroup.hpp"

task_group::task_group(net::any_io_executor exec)
    : cv_{ std::move(exec), net::steady_timer::time_point::max() }
{
}

template<typename CompletionToken>
auto task_group::adapt(CompletionToken&& completion_token)
{
    auto lg = std::lock_guard{ mtx_ };
    auto cs = css_.emplace(css_.end());

    class remover
    {
        task_group* tg_;
        decltype(css_)::iterator cs_;

    public:
        remover(task_group* tg, decltype(css_)::iterator cs)
            : tg_{ tg }, cs_{ cs }
        {
        }

        remover(remover&& other) noexcept
            : tg_{ std::exchange(other.tg_, nullptr) }, cs_{ other.cs_ }
        {
        }

        ~remover()
        {
            if (tg_)
            {
                auto lg = std::lock_guard{ tg_->mtx_ };
                if (tg_->css_.erase(cs_) == tg_->css_.end())
                    tg_->cv_.cancel();
            }
        }
    };

    return net::bind_cancellation_slot(
        cs->slot(),
        net::consign(
            std::forward<CompletionToken>(completion_token),
            remover{ this, cs }));
}

void task_group::emit(net::cancellation_type type)
{
    auto lg = std::lock_guard{ mtx_ };
    for (auto& cs : css_)
        cs.emit(type);
}

template<typename CompletionToken>
auto task_group::async_wait(CompletionToken&& completion_token)
{
    return net::async_compose<CompletionToken, void(boost::system::error_code)>(
        [this, scheduled = false](auto&& self, boost::system::error_code ec = {}) mutable
        {
            if (!scheduled)
                self.reset_cancellation_state(net::enable_total_cancellation());

            if (!self.cancelled() && ec == net::error::operation_aborted)
                ec = {};

            {
                auto lg = std::lock_guard{ mtx_ };
                if (!css_.empty() && !ec)
                {
                    scheduled = true;
                    return cv_.async_wait(std::move(self));
                }
            }

            if (!std::exchange(scheduled, true))
                return net::post(net::append(std::move(self), ec));

            self.complete(ec);
        },
        completion_token,
        cv_);
}

// 显式实例化常用模板
template auto task_group::adapt<decltype(net::detached)>(decltype(net::detached)&&);
template auto task_group::async_wait<decltype(net::detached)>(decltype(net::detached)&&);