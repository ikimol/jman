// Copyright (c) 2021 Miklos. All rights reserved.

#ifndef JMAN_CONTEXT_HPP
#define JMAN_CONTEXT_HPP

#include <memory>

namespace jman {

class job;
class manager;

using job_id = std::uint64_t;

class context {
    friend class job;

public:
    /// Get the id of the job
    job_id job_id() const;

    /// Check if the job was cancelled
    ///
    /// \note If the job was cancelled (i.e. this method returns true), you
    /// should clean up any resources and return JMAN_JOB_CANCELLED.
    bool cancel_requested() const;

    /// Set the current progress of the job
    void set_progress(float progress);

    /// Retrieve the user data (if any) that was provided when the job was added
    void* user_data();

private:
    explicit context(const std::shared_ptr<job>& j, manager* m);

private:
    std::weak_ptr<job> m_job;
    manager* m_manager;
};

} // namespace jman

#endif // JMAN_CONTEXT_HPP
