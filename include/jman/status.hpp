// Copyright (c) 2021 Miklos. All rights reserved.

#ifndef JMAN_STATUS_HPP
#define JMAN_STATUS_HPP

namespace jman {

/// Represents the different statuses a job can be in
enum class job_status {
    pending,    ///< The job was created and is waiting to be scheduled
    scheduled,  ///< The job has been scheduled and is waiting to be run
    running,    ///< The job is currently running
    cancelling, ///< The job received a cancellation request
    cancelled,  ///< The job has been cancelled
    succeeded,  ///< The job finished successfully
    failed,     ///< The job finished with an error
    unknown     ///< Unknown job status
};

} // namespace jman

#endif // JMAN_STATUS_HPP
