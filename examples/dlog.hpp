// Copyright (c) 2021 Miklos. All rights reserved.

#ifndef JMAN_DLOG_HPP
#define JMAN_DLOG_HPP

#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

class logger {
public:
    explicit logger(std::ostream& output, std::mutex& mutex) : m_output(&output), m_mutex(&mutex) {}

    ~logger() {
        std::lock_guard<std::mutex> lock(*m_mutex);
        (*m_output) << m_buffer.str() << '\n';
    }

    template <typename T>
    logger& operator<<(T&& value) {
        m_buffer << value;
        return *this;
    }

private:
    std::ostringstream m_buffer;
    std::ostream* m_output;
    std::mutex* m_mutex;
};

#define dlog(mtx) logger(std::cout, mtx)

template <template <typename...> class Map>
class basic_thread_logger {
public:
    using map_type = Map<std::thread::id, std::vector<std::string>>;

public:
    explicit basic_thread_logger(map_type& output) : m_output(&output) {}

    ~basic_thread_logger() {
        std::lock_guard<std::mutex> lock(s_mutex);
        (*m_output)[std::this_thread::get_id()].push_back(m_buffer.str());
    }

    template <typename T>
    basic_thread_logger& operator<<(T&& value) {
        m_buffer << value;
        return *this;
    }

private:
    std::ostringstream m_buffer;
    map_type* m_output;
    static std::mutex s_mutex;
};

template <template <typename...> class Map>
std::mutex basic_thread_logger<Map>::s_mutex;

using thread_logger = basic_thread_logger<std::unordered_map>;

#define thread_log(map) thread_logger(map)

#endif // JMAN_DLOG_HPP
