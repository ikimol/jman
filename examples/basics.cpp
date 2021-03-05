// Copyright (c) 2021 Miklos Molnar. All rights reserved.

#include "dlog.hpp"

#include <jman/jman.hpp>

class widget {
public:
    explicit widget(std::size_t value = 0) : m_value(value) {}

    std::size_t increment() { return ++m_value; }

    std::size_t value() const { return m_value; }

private:
    std::atomic<std::size_t> m_value = ATOMIC_VAR_INIT(0);
};

// clang-format off
static constexpr const char* status_names[] = {
    "pending",   "scheduled", "running", "cancelling",
    "cancelled", "succeeded", "failed",  "unknown"
};
// clang-format on

static thread_logger::map_type log_map;

static std::mutex cout_mutex;

struct status_listener {
    void operator()(jman::job_id id, jman::job_status status) {
        auto index = static_cast<std::size_t>(status);
        thread_log(log_map) << "job [" << id << "]: " << status_names[index];
    }
};

struct progress_listener {
    void operator()(jman::job_id id, float progress) {
        dlog(cout_mutex) << "job [" << id << "]: " << (progress * 100.f) << '%';
    }
};

static int widget_job(jman::context ctx) {
    auto& w = *static_cast<widget*>(ctx.user_data());
    for (std::size_t i = 0; i < 4; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds {500});
        w.increment();
    }
    return JMAN_JOB_SUCCESS;
}

static int generic_job(jman::context ctx) {
    for (std::size_t i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds {200});
        ctx.set_progress((i + 1.f) * 0.1f);
    }
    return JMAN_JOB_SUCCESS;
}

int main(int /* argc */, char* /* argv */[]) {
    auto main_thread_id = std::this_thread::get_id();

    jman::manager manager(3);
    manager.add_status_callback(status_listener {});
    manager.add_progress_callback(progress_listener {});

    widget w(0);

    auto id_0 = manager.add_job(jman::job::null_id, widget_job, {&w});
    manager.add_job(id_0, generic_job, {"category-a"});
    manager.add_job(id_0, generic_job, {"category-a"});
    manager.add_job(id_0, generic_job, {"category-b"});

    auto id_4 = manager.add_job(jman::job::null_id, widget_job, {&w});
    manager.add_job(id_4, generic_job, {"category-c"});
    manager.add_job(id_4, generic_job, {"category-d"});

    auto start = std::chrono::high_resolution_clock::now();

    // wait for all jobs to finish
    manager.wait_all_jobs();

    dlog(cout_mutex) << "wait ended";
    dlog(cout_mutex) << "-- scheduled: " << manager.scheduled_job_count();
    dlog(cout_mutex) << "-- running: " << manager.running_job_count();

    auto d = std::chrono::high_resolution_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    dlog(cout_mutex) << "-- finished in " << ms << "ms";

    for (const auto& pair : log_map) {
        if (pair.first == main_thread_id) {
            std::cout << "main [" << pair.first << ']';
        } else {
            std::cout << pair.first;
        }
        std::cout << '\n';
        for (const auto& msg : pair.second) {
            std::cout << '\t' << msg << '\n';
        }
    }

    dlog(cout_mutex) << "w.value = " << w.value() << '\n';

    return 0;
}
