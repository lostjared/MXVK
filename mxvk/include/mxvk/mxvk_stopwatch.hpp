#pragma once
#include <iostream>
#include <string_view>
#include <chrono>
#include <concepts>

// Define a C++20 Concept to enforce our policy contract
template <typename T>
concept ClockPolicy = requires(T clock, std::string_view name) {
    { clock.start() } -> std::same_as<void>;
    { clock.stop() } -> std::same_as<void>;
    { clock.echo(name) } -> std::same_as<void>;
    { clock.timePassed() } -> std::convertible_to<unsigned long>; 
};

// Uses Steady Clock for reliable performance profiling
class SteadyClockPolicy {
public:
    void start() {
        start_time = std::chrono::steady_clock::now();
    }
    
    void stop() {
        stop_time = std::chrono::steady_clock::now();
    }
    
    void echo(std::string_view name) const {
        std::cout << "Stopwatch [" << name << "]\n"
                  << "Timer was active for : " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(stop_time - start_time).count() 
                  << " Milliseconds\n";
    }
    
    unsigned long timePassed() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
    }
    
private:
    std::chrono::steady_clock::time_point start_time, stop_time;
};

// Uses High Resolution Clock for extreme precision
class HighResolutionClockPolicy {
public:
    void start() {
        start_time = std::chrono::high_resolution_clock::now();
    }
    
    void stop() {
        stop_time = std::chrono::high_resolution_clock::now();
    }
    
    void echo(std::string_view name) const {
        std::cout << "Stopwatch [" << name << "]\n"
                  << "Timer was active for : " 
                  << std::chrono::duration_cast<std::chrono::nanoseconds>(stop_time - start_time).count() 
                  << " Nanoseconds\n";
    }
    
    unsigned long timePassed() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time).count();
    }
    
private:
    std::chrono::high_resolution_clock::time_point start_time, stop_time;
};

// Timer template strictly constrained by the ClockPolicy concept
template<ClockPolicy T> 
class StopWatch {
public:
    explicit StopWatch(std::string_view name) : time_name(name) {
        Start(name);
    }
    
    ~StopWatch() {
        if (!m_stopped) {
            Stop();
        }
    }
    
    // Explicitly delete copy and move semantics to prevent timing bugs
    StopWatch(const StopWatch&) = delete;
    StopWatch& operator=(const StopWatch&) = delete;
    StopWatch(StopWatch&&) = delete;
    StopWatch& operator=(StopWatch&&) = delete;
    
    void Start(std::string_view name) {
        time_name = name;
        m_stopped = false;
        clock_interface.start();
    }
    
    void Stop() {
        if (m_stopped) return;
        clock_interface.stop();
        Echo(time_name);
        m_stopped = true;
    }
    
    void Echo(std::string_view name) const {
        clock_interface.echo(name);
    }
    
    unsigned long TimePassed() const {
        return clock_interface.timePassed();
    }
    
private:
    std::string_view time_name;
    T clock_interface;
    bool m_stopped = false;
};
