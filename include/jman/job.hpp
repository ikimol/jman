// Copyright (c) 2021 Miklos. All rights reserved.

#ifndef JMAN_JOB_HPP
#define JMAN_JOB_HPP

#include "concurrency.hpp"
#include "context.hpp"
#include "status.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>

#define JMAN_JOB_SUCCESS 0
#define JMAN_JOB_FAILURE 1
#define JMAN_JOB_CANCELLED -1
#define JMAN_JOB_UNKNOWN -2

namespace jman {

class context;
class manager;

using runnable = std::function<int(context)>;

struct options {
    /// Default constructor
    options() = default;

    /// Convenience constructor with a single user data
    options(void* user_data) /* NOLINT */ : user_data(user_data) {}

    /// Convenience constructor with a category and implicit concurrency
    options(std::string category) /* NOLINT */ :
    category(std::move(category)),
    concurrency(job_concurrency::category_exclusive) {}

    std::string category;
    job_concurrency concurrency = job_concurrency::any;

    bool interruptable = true;
    bool determinate = true;
    int progress_accuracy = 0;

    void* user_data = nullptr;
};

class job : public std::enable_shared_from_this<job> {
    friend class manager;

public:
    static constexpr job_id null_id = static_cast<std::uint64_t>(-1);

public:
    job_id id() const { return m_id; }

    job_status status() const { return m_status.load(std::memory_order_acquire); }

    void set_status(job_status st) { m_status.store(st, std::memory_order_release); }

    float progress() const { return m_progress.load(std::memory_order_acquire); }

    void set_progress(float p) { m_progress.store(p, std::memory_order_release); }

    runnable::result_type return_code() const { return m_rc.load(std::memory_order_acquire); }

    // options interface

    const std::string& category() const { return m_options.category; }

    job_concurrency concurrency() const { return m_options.concurrency; }

    int progress_accuracy() const { return m_options.progress_accuracy; }

    void* user_data() { return m_options.user_data; }

    // operations

    /// Run the piece of work
    runnable::result_type run() {
        auto rc = m_work(context {shared_from_this(), m_manager});
        m_rc.store(rc, std::memory_order_release);
        return rc;
    }

    /// Request to cancel this job (if running and interruptable)
    ///
    /// \return True of the job was cancelled, false otherwise
    bool cancel() {
        auto active = is_active();
        if (status() == job_status::pending) {
            set_status(job_status::cancelled);
            return true;
        } else if (active && !m_options.interruptable) {
            // job cannot be cancelled
        } else if (active) {
            set_status(job_status::cancelling);
            return true;
        }
        return false;
    }

    // status helpers

    /// Check if this job was executed
    bool did_run() const {
        auto st = m_status.load(std::memory_order_acquire);
        return st == job_status::succeeded || st == job_status::cancelled || st == job_status::failed;
    }

    /// Check if this job failed
    bool did_fail() const {
        auto st = m_status.load(std::memory_order_acquire);
        return st == job_status::failed;
    }

    /// Check if this job is either waiting to run or is running
    bool is_active() const {
        auto st = m_status.load(std::memory_order_acquire);
        return st == job_status::scheduled || st == job_status::running;
    }

    // helpers

    /// Check if the progress has deviated enough from its previous value to be
    /// reported
    bool should_update_progress(float p) const {
        if (!m_options.determinate) {
            return true;
        }

        if (m_options.progress_accuracy == 0) {
            return true;
        }

        if (p == 0.f || p == 1.f) {
            return true;
        }

        auto pow = std::pow(10, m_options.progress_accuracy);
        auto new_progress = std::floor(p * pow) / pow;
        auto current_progress = std::floor(progress() * pow) / pow;

        return new_progress != current_progress;
    }

    /// Check if this jobs and its parent's status permit scheduling
    bool can_schedule() const {
        if (status() != job_status::pending) {
            return false;
        }
        // check if parent succeeded
        if (auto parent = m_parent.lock()) {
            return parent->status() == job_status::succeeded;
        }
        // no parent means it can be scheduled freely
        return true;
    }

private:
    /// Create a new job with an id, the runnable code and some option
    job(job_id id, runnable work, options opt, manager* man, const std::shared_ptr<job>& parent) :
    m_id(id),
    m_work(std::move(work)),
    m_options(std::move(opt)),
    m_parent(parent),
    m_manager(man) {
        if (!m_work) {
            // TODO: Throw custom exception
            throw std::runtime_error("empty runnable");
        }
    }

private:
    const job_id m_id = 0;
    std::atomic<job_status> m_status = ATOMIC_VAR_INIT(job_status::unknown);

    std::mutex m_finish_mutex;
    std::condition_variable m_finish_cv;

    const runnable m_work;
    const options m_options;

    std::atomic<float> m_progress = ATOMIC_VAR_INIT(0.f);
    std::atomic<runnable::result_type> m_rc = ATOMIC_VAR_INIT(-2);

    std::weak_ptr<job> m_parent;
    manager* m_manager = nullptr;
};

} // namespace jman

#endif // JMAN_JOB_HPP
