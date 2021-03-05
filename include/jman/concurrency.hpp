// Copyright (c) 2021 Miklos. All rights reserved.

#ifndef JMAN_CONCURRENCY_HPP
#define JMAN_CONCURRENCY_HPP

namespace jman {

/// Represents the mutual exclusion rule for the job scheduler
enum class job_concurrency {
    any,                ///< All jobs are allowed to run in parallel
    category_exclusive, ///< Only 1 job in a given category is allowed to run
    none                ///< No jobs can be run in parallel
};

} // namespace jman

#endif // JMAN_CONCURRENCY_HPP
