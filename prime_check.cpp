#include "prime_checker.hpp" 

#include <algorithm>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <ctime>
#include <exception> 
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional> 
#include <sstream>
#include <stop_token>
#include <thread>
#include <unordered_set>
#include <utility>

namespace primechecker {

namespace {

// Removes leading and trailing whitespace from configuration text.
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\n\r");

    if (first == std::string::npos) {
        return {};
    } 

    const auto last = text.find_last_not_of(" \t\n\r");
    return text.substr(first, last - first + 1);
}

// Converts simple ASCII text to lowercase for boolean parsing.
std::string to_lower_ascii(std::string value) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }

    return value;
}

// Safely parses an unsigned integer without throwing standard conversion exceptions. 
std::uint64_t parse_unsigned_decimal(const std::string& text, const std::string& field_name) {
    if (text.empty()) {
        throw ConfigError(ConfigError::Kind::InvalidConfiguration, field_name + " cannot be empty.");
    }

    std::uint64_t value = 0;

    const char* begin = text.data();
    const char* end = text.data() + text.size();

    const auto [ptr, error] = std::from_chars(begin, end, value, 10); 

    if (error == std::errc::invalid_argument || ptr != end) {
        throw ConfigError(
            ConfigError::Kind::InvalidConfiguration, 
            field_name + " must contain valid unsigned decimal text '" + text + "'."
        );
    }

    return value;
}

// Parses either a decimal maximum value or the supported 2^n notation.
std::uint64_t parse_max_value(const std::string& text) {
    // Validate the exponent before shifting to avoid undefined behavior. 
    if (text.rfind("2^", 0) == 0) {
        const std::string exponent_text = text.substr(2);
        const std::uint64_t exponent = parse_unsigned_decimal(exponent_text, "Max Value exponent");

        if (exponent >= 64) {
            throw ConfigError(
                ConfigError::Kind::InvalidConfiguration,
                "Max Value exponent must be in the range 0..63 for uint64_t; "
                "received 2^" + exponent_text + "."
            );
        }

        const std::uint64_t value = std::uint64_t{1} << exponent;

        if (value < 2) {
            throw ConfigError(
                ConfigError::Kind::InvalidConfiguration,
                "Max Value must be at least 2."
            );
        }

        return value;
    }

    const std::uint64_t value = parse_unsigned_decimal(text, "Max Value");

    if (value < 2) {
        throw ConfigError(
            ConfigError::Kind::InvalidConfiguration,
            "Max Value must be at least 2."
        );
    }

    return value;
}

// Parses the optional true/false verbose loggign setting.
bool parse_bool(const std::string& text, const std::string& field_name) {
    const std::string normalized = to_lower_ascii(trim(text)); 

    if (normalized == "true") {
        return true;
    } 

    if (normalized == "false") {
        return false;
    } 

    throw ConfigError(
        ConfigError::Kind::InvalidConfiguration, 
        field_name + " must be true or false; received '" + text + "'."
    );
}

// Computes floor(sqrt(n)) using integer arithmetic only. 
std::uint64_t integer_sqrt(std::uint64_t n) {
    if (n < 2) {
        return n;
    } 

    // UINT32_MAX is the largest possible integer sqrt of a uint64_t.
    std::uint64_t low = 1;

    std::uint64_t high = std::min<std::uint64_t>(
        n, static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max()
        )
    );

    std::uint64_t answer = 1;

    while (low <= high) {
        const std::uint64_t middle = low + (high - low) / 2;

        // Division avoids overflow from middle * middle.
        if (middle <= n / middle) {
            answer = middle;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    } 

    return answer;
}

// Caps B1 workers so every active worker receives at least one candidate. 
std::size_t useful_b1_workers(const Config& config) {
    const std::uint64_t candidate_count = config.max_value - 1; // Candidate values are 2..max_value.
    const std::uint64_t requested = static_cast<std::uint64_t>(config.requested_threads);

    return static_cast<std::size_t>(std::min(candidate_count, requested));
}

// Caps B2 workers based on the available odd divisor positions.
std::size_t useful_b2_workers(const Config& config) {
    const std::uint64_t root = integer_sqrt(config.max_value);

    // Odd divisor positions are 3, 5, 7, ... up to sqrt(max_value).
    const std::uint64_t odd_divisor_slots = (root < 3) ? 0 : ((root - 3) / 2 + 1);

    // Keep one worker for candidates that require only specific-case handling.
    const std::uint64_t useful = std::max<std::uint64_t>(1, odd_divisor_slots);

    const std::uint64_t requested = static_cast<std::uint64_t>(config.requested_threads);

    return static_cast<std::size_t>(std::min(useful, requested));
}

// Formats steady-clock durations using both microseconds and milliseconds.
std::string duration_text(std::chrono::steady_clock::duration duration) {
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    const double milliseconds = std::chrono::duration<double, std::milli>(duration).count();

    std::ostringstream out;

    out << microseconds << " us ("
        << std::fixed << std::setprecision(3)
        << milliseconds << " ms)";
    
    return out.str();
}

// Creates a consistent logical worker identifier for console output. 
std::string worker_prefix(std::size_t thread_id) {
    std::ostringstream out; 
    out << "Thread " << std::setw(2) << thread_id;
    return out.str();
}

// Implements B1 by assigning each worker one contiguous candidate range. 
class RangeDivisionStrategy final : public WorkDivisionStrategy {

public: 
    RunStatistics run(const Config& config, OutputManager& output) override {
        RunStatistics statistics;
        statistics.active_workers = useful_b1_workers(config);

        if (statistics.active_workers < config.requested_threads) {
            std::ostringstream message;

            message << "B1 active workers capped at " << statistics.active_workers
                    << " because there are only " << (config.max_value - 1)
                    << " candidate values in 2.." << config.max_value << ".";

            output.print_line(message.str());
        }

        // A2 gives each worker its own vector to avoid result-lock contention.
        std::vector<std::vector<PrimeResult>>per_thread_results(statistics.active_workers);

        // Atomic counting avoids a mutex for the shared prime total.
        std::atomic<std::uint64_t> total_primes{0};

        std::atomic<bool> cancel{false};
        std::mutex error_mutex; 
        std::exception_ptr worker_error;

        const std::uint64_t candidate_count = config.max_value - 1;
        const std::uint64_t active = static_cast<std::uint64_t>(statistics.active_workers);
        const std::uint64_t base_size = candidate_count / active;
        const std::uint64_t remainder = candidate_count % active; 

        std::vector<std::jthread> workers;
        workers.reserve(statistics.active_workers);

        std::uint64_t offset = 0;
        const auto computation_start = std::chrono::steady_clock::now();

        // Create one worker for each disjoint B1 range. 
        for (std::size_t thread_id = 0; thread_id < statistics.active_workers; thread_id++) {
            const std::uint64_t length = base_size + (static_cast<std::uint64_t>(thread_id) < remainder ? 1 : 0);

            const std::uint64_t range_start = 2 + offset;
            const std::uint64_t range_end = range_start + length - 1;
            offset += length;

            workers.emplace_back(
                [&, thread_id, range_start, range_end] (std::stop_token stop_token) {
                    try {
                        {
                            std::ostringstream line;

                            line << worker_prefix(thread_id) << " | B1 range: "
                                 << range_start << '-' << range_end;

                            output.print_line(line.str());
                        }

                        // Each worker checks only its assigned candidate range. 
                        for (std::uint64_t n = range_start;; n++) {
                            if (stop_token.stop_requested() || cancel.load(std::memory_order_relaxed)) {
                                break;
                            }

                            if (is_prime_sequential(n)) {
                                PrimeResult result{n, std::chrono::system_clock::now(), thread_id};
                                total_primes.fetch_add(1, std::memory_order_relaxed);

                                if (config.printing_variant == PrintingVariant::Immediate) {
                                    // A1 prints immediately using synchronized console access. 
                                    output.print_prime(result);
                                } else {
                                    // A2 stores locally until all workers have finished.
                                    per_thread_results[thread_id].push_back(result);
                                }
                            }

                            // Prevent overflow if range_end is UINT64_MAX
                            if (n == range_end) {
                                break;
                            }
                        }

                        output.print_line(worker_prefix(thread_id) + " | B1 worker finished");

                    } catch (...) {
                        // Tell other workers to stop if one worker fails.
                        cancel.store(true, std::memory_order_relaxed);
                        std::lock_guard<std::mutex>lock(error_mutex);

                        if (!worker_error) {
                            worker_error = std::current_exception();
                        }
                    }
            });
        }

        // Join every worker before accessing final results. 
        for (auto& worker : workers) {
            worker.join(); 
        }

        const auto computation_end = std::chrono::steady_clock::now();
        statistics.computation_time = computation_end - computation_start;

        if (worker_error) {
            std::rethrow_exception(worker_error);
        } 

        statistics.total_primes = total_primes.load(std::memory_order_relaxed); 

        if (config.printing_variant == PrintingVariant::Batch) {
            std::size_t total_result_count = 0;

            for (const auto& local : per_thread_results) {
                total_result_count += local.size();
            } 

            statistics.batch_results.reserve(total_result_count);

            // Merge worker-local A2 results after synchronization is complete.
            for (auto& local : per_thread_results) {
                statistics.batch_results.insert(
                    statistics.batch_results.end(), 
                    std::make_move_iterator(local.begin()), 
                    std::make_move_iterator(local.end())
                );
            }
        }

        return statistics;
    }
};

// Persistent worker pool used by the B2 divisor-testing strategy
class DivisorWorkerPool {

public: 
    DivisorWorkerPool(std::size_t worker_count, PrintingVariant printing_variant, bool verbose_divisibility, 
        OutputManager& output) : worker_count_(worker_count), 
            printing_variant_(printing_variant), 
            verbose_divisibility_(verbose_divisibility), 
            output_(output), per_thread_results_(worker_count) {
            
        workers_.reserve(worker_count_);

        // B2 workers are created once and reused for every candidate.
        for (std::size_t thread_id = 0; thread_id < worker_count_; thread_id++) {
            workers_.emplace_back([this, thread_id] (std::stop_token stop_token) {
                worker_loop(thread_id, stop_token);
            });
        }
    }

    DivisorWorkerPool(const DivisorWorkerPool&) = delete; 
    DivisorWorkerPool& operator = (const DivisorWorkerPool&) = delete; 
    
    ~DivisorWorkerPool() {
        shutdown();
    }

    // Publishes one candidate and waits for all workers to finish its divisor work.
    void process_candidate(std::uint64_t candidate) {
        std::unique_lock<std::mutex> lock(state_mutex_); 

        if (worker_error_) {
            std::rethrow_exception(worker_error_);
        } 

        candidate_ = candidate;
        completed_workers_ = 0;

        // Reset early-composite detection for candidate.
        composite_.store(false, std::memory_order_release);
        generation_++;

        const std::uint64_t this_generation = generation_;

        // Wake workers without holding the state mutex.
        lock.unlock();
        work_cv_.notify_all();
        lock.lock();

        // Do not move to next candidate until current round is complete.
        done_cv_.wait(
            lock, [&] {
                return completed_generation_ == this_generation || worker_error_ != nullptr;
        });

        if (worker_error_) {
            std::rethrow_exception(worker_error_);
        }
    }

    // Requests clean termination of every persistent B2 worker. 
    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> lock(state_mutex_); 
            stopping_ = true; 
        } 

        abort_.store(true, std::memory_order_relaxed); 

        for (auto& worker : workers_) {
            worker.request_stop();
        } 

        work_cv_.notify_all();

        // Destroying jthreads also joins them safely.
        workers_.clear();
    }

    [[nodiscard]] std::uint64_t total_primes() const noexcept {
        return total_primes_.load(std::memory_order_relaxed);
    }

    // Merges the A2 worker-local result vectors after computation.
    [[nodiscard]] std::vector<PrimeResult> take_results() {
        std::size_t result_count = 0;

        for (const auto& local : per_thread_results_) {
            result_count += local.size();
        }

        std::vector<PrimeResult> merged;
        merged.reserve(result_count); 

        for (auto& local : per_thread_results_) {
            merged.insert(
                merged.end(), 
                std::make_move_iterator(local.begin()), 
                std::make_move_iterator(local.end())
            );
        }

        return merged;
    }

private:
    // Persistent worker loop waits for a new candidate generation.
    void worker_loop(std::size_t thread_id, std::stop_token stop_token) noexcept {
        try {
            output_.print_line(worker_prefix(thread_id) + " | B2 persistent worker started"); 
            std::uint64_t seen_generation = 0;

            while (!stop_token.stop_requested()) {
                std::uint64_t candidate = 0;
                std::uint64_t local_generation = 0;

                {
                    std::unique_lock<std::mutex> lock(state_mutex_);

                    // Sleep until a new candidate is assigned or shutdown begins.
                    work_cv_.wait(
                        lock, stop_token, [&] {
                            return stopping_ || generation_ != seen_generation;
                    });

                    if (stop_token.stop_requested() || stopping_) {
                        break;
                    }

                    candidate = candidate_;
                    local_generation = generation_;
                    seen_generation = generation_;
                }

                // Perform only this worker's divisor lane.
                perform_divisor_work(candidate, thread_id);
                bool is_last_worker = false;

                {
                    std::lock_guard<std::mutex> lock(state_mutex_);

                    if (stopping_) {
                        break;
                    } 

                    completed_workers_++;

                    is_last_worker = (completed_workers_ == worker_count_);
                } 

                if (is_last_worker) {
                    // No factor found after every lane finished means the candidate is prime.
                    if (!composite_.load(std::memory_order_acquire) && !abort_.load(std::memory_order_relaxed)) {
                        PrimeResult result {candidate, std::chrono::system_clock::now(), thread_id};
                        total_primes_.fetch_add(1, std::memory_order_relaxed);

                        if (printing_variant_ == PrintingVariant::Immediate) {
                            // A1 prints the prime immediately.
                            output_.print_prime(result);
                        } else {
                            // A2 stores the result in the finalizing worker's local vector.
                            per_thread_results_[thread_id].push_back(result);
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        completed_generation_ = local_generation;
                    }

                    // Wake the controller so it can submit the next candidate.
                    done_cv_.notify_one();
                }
            }

            output_.print_line(worker_prefix(thread_id) + " | B2 persistent worker stopped");
        } catch (...) {
            // Abort the pool if any worker encounters an exception.
            abort_.store(true, std::memory_order_relaxed);

            {
                std::lock_guard<std::mutex> lock(state_mutex_);

                if (!worker_error_) {
                    worker_error_ = std::current_exception();
                } 

                stopping_ = true;
            }

            work_cv_.notify_all();
            done_cv_.notify_all();
        }
    }

    // Tests one worker's share of the divisor sequence for a candidate.
    void perform_divisor_work(std::uint64_t candidate, std::size_t thread_id) {
        if (candidate < 2) {
            mark_composite(candidate, 1, thread_id);
            return;
        } 

        // 2 is prime and requires no divisor search.
        if (candidate == 2) {
            if (verbose_divisibility_ && thread_id == 0) {
                output_.print_line(
                    worker_prefix(thread_id) + " | Candidate: " + std::to_string(candidate) + " | Testing divisor: 2"
                );
            }

            return;
        }

        // Even numbers greater than 2 are composite immediately.
        if ((candidate % 2) == 0) {
            if (thread_id == 0) {
                if (verbose_divisibility_) {
                    output_.print_line(
                        worker_prefix(thread_id) + " | Candidate: " + 
                        std::to_string(candidate) + " | Testing divisor: 2");
                }

                mark_composite(candidate, 2, thread_id);
            }

            return;
        }

        // Each B2 worker receives a different lane of odd divisors. 
        const std::uint64_t first_divisor = 3 + 2 * static_cast<std::uint64_t>(thread_id);
        const std::uint64_t step = 2 * static_cast<std::uint64_t>(worker_count_);

        // This worker has no useful divsor if first value exceeds sqrt(candidate).
        if (first_divisor > candidate / first_divisor) {
            return;
        }

        if (verbose_divisibility_) {
            std::ostringstream line;

            line << worker_prefix(thread_id) << " | Candidate: " << candidate 
                 << " | Checking odd divisors from " << first_divisor << " with stride " 
                 << step << " (while d <= n/d)";

            output_.print_line(line.str());
        } 

        for (std::uint64_t divisor = first_divisor; divisor <= candidate / divisor; /*divisor increment below*/) {
            // Stop early if another worker already discovered a factor.
            if (composite_.load(std::memory_order_acquire) || abort_.load(std::memory_order_relaxed)) {
                break;
            } 

            if ((candidate % divisor) == 0) {
                mark_composite(candidate, divisor, thread_id); 
                break;
            }

            // Guard the divisor increment against uint64_t overflow.
            if (divisor > std::numeric_limits<std::uint64_t>::max() - step) {
                break;
            }

            divisor += step;
        }
    }

    // Atomically records the first factor discovered for a candidate. 
    void mark_composite(std::uint64_t candidate, std::uint64_t factor, std::size_t thread_id) {
        bool expected = false; 

        if (composite_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            if (verbose_divisibility_) {
                std::ostringstream line; 

                line << worker_prefix(thread_id) << " | Candidate: " << candidate 
                     << " | Factor found: " << factor << " | Canceling remaining divisor checks";

                output_.print_line(line.str());
            }
        }
    }

    const std::size_t worker_count_; 
    const PrintingVariant printing_variant_;
    const bool verbose_divisibility_;

    OutputManager& output_;

    // Per-thread A2 storage avoids a shared result-vector mutex. 
    std::vector<std::vector<PrimeResult>> per_thread_results_;
    std::atomic<std::uint64_t> total_primes_{0};

    // Atomics provide low-cost early cancellation during divisor testing. 
    std::atomic<bool> composite_{false};
    std::atomic<bool> abort_{false};

    // These members coordinate reusable B2 candidate rounds. 
    std::mutex state_mutex_;
    std::condition_variable_any work_cv_;
    std::condition_variable done_cv_;

    std::uint64_t candidate_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t completed_generation_ = 0;

    std::size_t completed_workers_ = 0;

    bool stopping_ = false; 

    std::exception_ptr worker_error_;
    
    // Declared last so worker threads are cleaned up safely during destruction.
    std::vector<std::jthread> workers_;
};

// Implements B2 by parallelizing divisor checks for each candidate.
class DivisibilityDivisionStrategy final : public WorkDivisionStrategy {

public:
    RunStatistics run(const Config& config, OutputManager& output) override {
        RunStatistics statistics;
        statistics.active_workers = useful_b2_workers(config);

        if (statistics.active_workers < config.requested_threads) {
            const std::uint64_t root = integer_sqrt(config.max_value);
            const std::uint64_t odd_slots = (root < 3) ? 0 : ((root - 3) / 2 + 1);
            std::ostringstream message;

            message << "B2 active workers capped at" << statistics.active_workers
                    << " (requested " << config.requested_threads << "). ";
                
            if (odd_slots == 0) {
                message << "No odd divisor positions exist up to sqrt(Max Value), "
                            "so one worker handles special cases.";
            } else {
                message << "At most " << odd_slots 
                        << " odd divisor lanes can be useful up to sqrt(Max Value).";
            }

            output.print_line(message.str());
        } 

        // Avoid expensive synchronized rounds for obvious even composites. 
        output.print_line(
            "B2 pre-check: even candidates greater than 2 are rejected before "
            "parallel divisor rounds."
        );

        if (config.verbose_divisibility) {
            output.print_line(
                "Warning: Verbose B2 divisibility logging is enabled; "
                "console I/O will substantially distort benchmark timing."
            );
        }

        const auto computation_start = std::chrono::steady_clock::now();

        // One persistent pool handles all B2 candidate rounds. 
        DivisorWorkerPool pool(
            statistics.active_workers, 
            config.printing_variant, 
            config.verbose_divisibility, 
            output
        );

        // process the special-case prime 2.
        pool.process_candidate(2); 

        // Only odd candidates require parallel odd-divisor testing. 
        if (config.max_value >= 3) {
            for (std::uint64_t candidate = 3;;) {
                pool.process_candidate(candidate); 

                // Stop before adding 2 would exceed the configured maximum.
                if (candidate >= config.max_value - 1) {
                    break;
                }

                candidate += 2;
            }
        }

        // Stop and join all persistent workers before collecting results. 
        pool.shutdown();

        const auto computation_end = std::chrono::steady_clock::now();
        statistics.computation_time = computation_end - computation_start;
        statistics.total_primes = pool.total_primes();

        if (config.printing_variant == PrintingVariant::Batch) {
            statistics.batch_results = pool.take_results();
        }

        return statistics;
    }
};

}; // namespace

}; // namespace primechecker