// Copyright (c) 2021 Miklos. All rights reserved.

#ifndef JMAN_MANAGER_HPP
#define JMAN_MANAGER_HPP

#include <shard/algorithm/container_utils.hpp>
#include <shard/algorithm/stl_wrappers.hpp>
#include <shard/concurrency/channel.hpp>

#include "context.hpp"
#include "job.hpp"
#include "status.hpp"

#include <functional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace jman {

using status_callback = std::function<void(job_id, job_status)>;
using progress_callback = std::function<void(job_id, float)>;

class manager {
    friend class context;

public:
    /// Default construct with the maximum number of available hardware threads
    manager() : manager(std::max(std::thread::hardware_concurrency(), 2u) - 1u) {}

    /// Construct with a number of threads
    explicit manager(std::size_t thread_count) {
        m_threads.reserve(thread_count);
        // launch the requested number of threads
        for (std::size_t i = 0; i < thread_count; ++i) {
            m_threads.emplace_back(&manager::thread_fn, this, i);
        }
    }

    ~manager() {
        m_job_channel.close();
        for (auto& t : m_threads) {
            t.join();
        }
    }

    // job operations

    /// Add a job and immediately try to schedule it for execution
    job_id add_job(job_id parent_id, runnable work, options opts = {}) {
        // initialize the category counter
        if (!opts.category.empty()) {
            std::lock_guard<std::mutex> lock(m_category_map_mutex);
            if (!shard::has_key(m_active_categories, opts.category)) {
                m_active_categories.emplace(opts.category, 0);
            }
        }

        // check if a parent should be set
        std::shared_ptr<job> parent;
        if (parent_id != job::null_id) {
            parent = job_for_id(parent_id);
        }

        // create the new job
        auto new_id = m_next_id++;
        auto new_job = new job(new_id, std::move(work), std::move(opts), this, parent);

        cleanup_jobs();

        // take ownership of the new dynamically allocated job
        m_jobs.emplace_back(new_job);

        // set the status the new job and issue a scheduling
        update_status(m_jobs.back(), job_status::pending);
        schedule_jobs();

        return new_id;
    }

    /// Wait for the job to finish
    ///
    /// \param id The id of the job as returned by add_job()
    void wait_job(job_id id) {
        auto j = job_for_id(id);
        std::unique_lock<std::mutex> lock(j->m_finish_mutex);
        // unblock if the job has finished running
        j->m_finish_cv.wait(lock, [j] { return j->did_run(); });
    }

    /// Wait for all the scheduled and running jobs to finish
    void wait_all_jobs() {
        std::unique_lock<std::mutex> lock(m_job_count_mutex);
        // unblock if the channel is empty and there's no job running
        m_job_count_cv.wait(lock, [this] { return m_job_channel.is_empty() && m_running_job_count == 0; });
    }

    /// Request to cancel the job
    ///
    /// \note Only jobs that are interruptable may be cancelled.
    ///
    /// \param id The id of the job as returned by add_job()
    ///
    /// \return True of the job was cancelled, false otherwise
    bool cancel_job(job_id id) {
        if (auto j = job_for_id(id)) {
            return j->cancel();
        }
        return false;
    }

    /// Get the status of the job
    ///
    /// \param id The id of the job as returned by add_job()
    ///
    /// \return The status of the job, or status_t::unknown if the job id is
    /// invalid.
    job_status get_job_status(job_id id) const {
        if (auto j = job_for_id(id)) {
            return j->status();
        }
        return job_status::unknown;
    }

    /// Get the return code of the job
    ///
    /// \param id The id of the job as returned by add_job()
    ///
    /// \return The return code of the job, or SHARD_JOB_UNKNOWN if the job has
    /// not yet run or in case of an invalid job id.
    runnable::result_type get_job_return_code(job_id id) const {
        if (auto j = job_for_id(id)) {
            return j->return_code();
        }
        return JMAN_JOB_UNKNOWN;
    }

    // manager operations

    /// Add a callback that will be called every time the status of a job
    /// changes
    void add_status_callback(const status_callback& callback) {
        assert(callback);
        m_status_callbacks.push_back(callback);
    }

    /// Add a callback that will be called every time the progress of a job
    /// changes
    void add_progress_callback(const progress_callback& callback) {
        assert(callback);
        m_progress_callbacks.push_back(callback);
    }

    /// Get the number of worker threads
    std::size_t thread_count() const { return m_threads.size(); }

    /// Get the number of jobs running at the moment
    std::size_t running_job_count() const {
        std::lock_guard<std::mutex> lock(m_job_count_mutex);
        return m_running_job_count;
    }

    /// Get the number of jobs added to the scheduler queue
    std::size_t scheduled_job_count() const { return m_job_channel.size(); }

private:
    /// Update the status of the job
    void update_status(const std::shared_ptr<job>& j, job_status st) {
        j->set_status(st);
        if (st == job_status::succeeded) {
            j->set_progress(1.f);
        }

        // invoke status callbacks
        for (const auto& cb : m_status_callbacks) {
            cb(j->id(), st);
        }
    }

    /// Update the progress of the job
    void update_progress(const std::shared_ptr<job>& j, float p) {
        assert(p >= 0.f && p <= 1.f);

        if (j->progress() != p) {
            j->set_progress(p);

            // invoke progress callbacks
            for (const auto& cb : m_progress_callbacks) {
                cb(j->id(), p);
            }
        }
    }

    /// Schedule every job that can be scheduled at the moment
    void schedule_jobs() {
        for (const auto& j : m_jobs) {
            if (!schedule_job(j)) {
                break;
            }
        }
    }

    /// Check if the job can be scheduled and if so, add it to the queue
    ///
    /// \return false if no more jobs can be scheduled at the moment
    bool schedule_job(const std::shared_ptr<job>& j) {
        if (!j->can_schedule()) {
            // this job cannot be scheduled but maybe another can
            return true;
        }

        switch (j->concurrency()) {
        case job_concurrency::any:
            do_schedule_job(j);
            break;
        case job_concurrency::category_exclusive:
            // check active categories
            {
                std::lock_guard<std::mutex> lock(m_category_map_mutex);
                if (m_active_categories[j->category()] > 0) {
                    // this job cannot be scheduled but maybe another can
                    return true;
                }
            }
            do_schedule_job(j);
            break;
        case job_concurrency::none:
            if (running_job_count() == 0) {
                do_schedule_job(j);
                // no other job can be scheduled
                return false;
            }
            break;
        }

        return true;
    }

    void do_schedule_job(const std::shared_ptr<job>& j) {
        j->set_status(job_status::scheduled);
        m_job_channel.put(j);
    }

    void cleanup_jobs() {
        static const auto matcher = [](const std::shared_ptr<job>& j) {
            return j->did_run();
        };
        // TODO: Delete the job pointers
        shard::erase_if(m_jobs, matcher);
    }

    std::shared_ptr<job> job_for_id(job_id id) const {
        static const auto matcher = [id](const std::shared_ptr<job>& j) {
            return j->id() == id;
        };
        auto it = shard::find_if(m_jobs, matcher);
        return it != m_jobs.end() ? *it : nullptr;
    }

    void thread_fn(std::size_t /* id */) {
        std::shared_ptr<job> j;
        while (m_job_channel.wait_get(j)) {
            // increment running job count
            {
                std::lock_guard<std::mutex> lock(m_job_count_mutex);
                ++m_running_job_count;
            }

            // mark job as running
            update_status(j, job_status::running);

            // update active categories
            {
                std::lock_guard<std::mutex> lock(m_category_map_mutex);
                if (!j->category().empty()) {
                    ++m_active_categories[j->category()];
                }
            }

            // execute the job
            auto rc = j->run();

            // set the 'finished' status of the job under the mutex (even though
            // it's atomic) in order to correctly publish the modification to
            // the waiting thread
            {
                std::lock_guard<std::mutex> lock(j->m_finish_mutex);

                // update the job status based on the return code
                if (rc == JMAN_JOB_SUCCESS) {
                    update_status(j, job_status::succeeded);
                } else if (rc == JMAN_JOB_CANCELLED) {
                    update_status(j, job_status::cancelled);
                } else {
                    update_status(j, job_status::failed);
                }
            }

            // notify waiting threads about the finished job
            j->m_finish_cv.notify_all();

            // update active categories
            {
                std::lock_guard<std::mutex> lock(m_category_map_mutex);
                if (!j->category().empty()) {
                    --m_active_categories[j->category()];
                }
            }

            // try to schedule some pending jobs
            schedule_jobs();

            // decrement running job count
            {
                std::lock_guard<std::mutex> lock(m_job_count_mutex);
                --m_running_job_count;
            }

            // notify waiting threads about the finished job
            m_job_count_cv.notify_all();
        }
    }

private:
    std::size_t m_next_id = 0;
    std::vector<std::shared_ptr<job>> m_jobs;

    std::unordered_map<std::string, int> m_active_categories;
    std::mutex m_category_map_mutex;

    std::vector<std::thread> m_threads;
    shard::channel<std::shared_ptr<job>> m_job_channel;

    std::size_t m_running_job_count = 0;
    mutable std::mutex m_job_count_mutex;
    std::condition_variable m_job_count_cv;

    std::vector<status_callback> m_status_callbacks;
    std::vector<progress_callback> m_progress_callbacks;
};

// context class implementation

inline context::context(const std::shared_ptr<job>& j, manager* man) : m_job(j), m_manager(man) {}

inline job_id context::job_id() const {
    auto j = m_job.lock();
    assert(j);
    return j->id();
}

inline bool context::cancel_requested() const {
    auto j = m_job.lock();
    assert(j);
    return j->status() == job_status::cancelling;
}

inline void context::set_progress(float progress) {
    auto j = m_job.lock();
    assert(j);
    if (j->should_update_progress(progress)) {
        m_manager->update_progress(j, progress);
    }
}

inline void* context::user_data() {
    auto j = m_job.lock();
    assert(j);
    return j->user_data();
}

} // namespace jman

#endif // JMAN_MANAGER_HPP
