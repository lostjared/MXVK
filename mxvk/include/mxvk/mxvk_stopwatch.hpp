#pragma once
#include <chrono>
#include <concepts>
#include <iostream>
#include <string_view>

/**
 * @brief Defines the interface required by a stopwatch clock policy.
 *
 * A compatible policy must support starting and stopping its clock, printing
 * the measured interval, and querying the currently elapsed time.
 *
 * @tparam T Clock policy type to validate.
 */
template <typename T>
concept ClockPolicy = requires(T clock, std::string_view name) {
    { clock.start() } -> std::same_as<void>;
    { clock.stop() } -> std::same_as<void>;
    { clock.echo(name) } -> std::same_as<void>;
    { clock.timePassed() } -> std::convertible_to<unsigned long>;
};

/**
 * @brief Stopwatch policy backed by `std::chrono::steady_clock`.
 *
 * The steady clock is monotonic, making this policy suitable for measuring
 * elapsed wall-clock time even if the system clock is adjusted. Elapsed
 * queries are returned in milliseconds.
 */
class SteadyClockPolicy {
  public:
    /** @brief Record the beginning of a timed interval. */
    void start() {
        start_time = std::chrono::steady_clock::now();
    }

    /** @brief Record the end of a timed interval. */
    void stop() {
        stop_time = std::chrono::steady_clock::now();
    }

    /**
     * @brief Print the recorded interval in milliseconds and nanoseconds.
     * @param name Descriptive name shown with the timing result.
     *
     * Call stop() before calling this function.
     */
    void echo(std::string_view name) const {
        const auto elapsed = stop_time - start_time;
        std::cout << "Stopwatch [" << name << "]\n"
                  << "Timer was active for : "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                  << " Milliseconds\n"
                  << "Timer was active for : "
                  << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
                  << " Nanoseconds\n";
    }

    /**
     * @brief Return the time elapsed since the most recent start().
     * @return Elapsed time in milliseconds.
     */
    unsigned long timePassed() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
    }

  private:
    std::chrono::steady_clock::time_point start_time, stop_time;
};

/**
 * @brief Stopwatch policy backed by `std::chrono::high_resolution_clock`.
 *
 * This policy provides the clock's finest available resolution. Elapsed
 * queries are returned in nanoseconds.
 */
class HighResolutionClockPolicy {
  public:
    /** @brief Record the beginning of a timed interval. */
    void start() {
        start_time = std::chrono::high_resolution_clock::now();
    }

    /** @brief Record the end of a timed interval. */
    void stop() {
        stop_time = std::chrono::high_resolution_clock::now();
    }

    /**
     * @brief Print the recorded interval in milliseconds and nanoseconds.
     * @param name Descriptive name shown with the timing result.
     *
     * Call stop() before calling this function.
     */
    void echo(std::string_view name) const {
        const auto elapsed = stop_time - start_time;
        std::cout << "Stopwatch [" << name << "]\n"
                  << "Timer was active for : "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                  << " Milliseconds\n"
                  << "Timer was active for : "
                  << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
                  << " Nanoseconds\n";
    }

    /**
     * @brief Return the time elapsed since the most recent start().
     * @return Elapsed time in nanoseconds.
     */
    unsigned long timePassed() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time).count();
    }

  private:
    std::chrono::high_resolution_clock::time_point start_time, stop_time;
};

/**
 * @brief RAII stopwatch using a selectable clock policy.
 *
 * Construction starts the timer immediately. Stop() prints the result and is
 * safe to call more than once. If the stopwatch is still running when it is
 * destroyed, the destructor stops it and prints the result automatically.
 *
 * @tparam T Clock policy satisfying ClockPolicy.
 *
 * @note The stopwatch stores its name as a `std::string_view`. The referenced
 * string must remain valid for the lifetime of the stopwatch.
 */
template <ClockPolicy T>
class StopWatch {
  public:
    /**
     * @brief Construct and immediately start a stopwatch.
     * @param name Descriptive name printed with the timing result.
     */
    explicit StopWatch(std::string_view name) : time_name(name) {
        Start(name);
    }

    /** @brief Stop and print the timer if it is still running. */
    ~StopWatch() {
        if (!m_stopped) {
            Stop();
        }
    }

    /** @brief Stopwatches cannot be copied. */
    StopWatch(const StopWatch &) = delete;
    /** @brief Stopwatches cannot be copy-assigned. */
    StopWatch &operator=(const StopWatch &) = delete;
    /** @brief Stopwatches cannot be moved. */
    StopWatch(StopWatch &&) = delete;
    /** @brief Stopwatches cannot be move-assigned. */
    StopWatch &operator=(StopWatch &&) = delete;

    /**
     * @brief Start a new timed interval.
     * @param name Descriptive name printed with the timing result.
     *
     * Calling this function restarts the stopwatch and replaces its name.
     */
    void Start(std::string_view name) {
        time_name = name;
        m_stopped = false;
        clock_interface.start();
    }

    /**
     * @brief Stop the current interval and print its duration.
     *
     * Repeated calls have no effect until Start() begins another interval.
     */
    void Stop() {
        if (m_stopped)
            return;
        clock_interface.stop();
        Echo(time_name);
        m_stopped = true;
    }

    /**
     * @brief Print the interval recorded by the clock policy.
     * @param name Descriptive name shown with the timing result.
     */
    void Echo(std::string_view name) const {
        clock_interface.echo(name);
    }

    /**
     * @brief Query the time elapsed since the most recent Start().
     * @return Elapsed time in the unit defined by the selected clock policy.
     *
     * SteadyClockPolicy returns milliseconds, while
     * HighResolutionClockPolicy returns nanoseconds.
     */
    unsigned long TimePassed() const {
        return clock_interface.timePassed();
    }

  private:
    std::string_view time_name;
    T clock_interface;
    bool m_stopped = false;
};
