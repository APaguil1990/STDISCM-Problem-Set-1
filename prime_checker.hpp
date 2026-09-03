#pragma once

#include <chrono>
#include <cstddef> 
#include <cstdint> 
#include <mutex> 
#include <stdexcept> 
#include <string> 
#include <vector>

namespace primechecker {

// Defines how prime results are printed
enum class PrintingVariant{
    Immediate,  // A1
    Batch,      // A2
};

// Defines how multithreaded work is divided
enum class DivisionScheme {
    Range,          // B1
    Divisibility    // B2
};

// Stores validated configuration settings loaded from config.txt
struct Config {
    std::size_t requested_threads;
    std::uint64_t max_value;
    PrintingVariant printing_variant;
    DivisionScheme division_scheme;
    bool verbose_divisibility = false;

    // Returns the combined variant name, such as "A1-B2".
    [[nodiscard]] std::string variant_name() const;
};

// Stores information about a discovered prime number
struct PrimeResult {
    std::uint64_t prime;
    std::chrono::system_clock::time_point timestamp;
    std::size_t thread_id;
};

// Stores timing, worker count, and results from one program run.
struct RunStatistics {
    std::uint64_t total_primes = 0;
    std::size_t active_workers = 0;
    std::chrono::steady_clock::duration computation_time{};
    std::vector<PrimeResult> batch_results;
};

// Represents an error found while reading or validating configuration.
class ConfigError : public std::runtime_error {
public:
    enum class Kind {
        MissingRequiredField, 
        InvalidConfiguration
    };

    ConfigError(Kind kind, const std::string& message);

    // Identifies the type of configuration error.
    [[nodiscard]] Kind kind() const noexcept;

private:
    Kind kind_;
};

// Loads and validates configuration settings from a file.
class ConfigLoader {
public:
    [[nodiscard]] static Config load_from_file(const std::string& filename);
};

// Centralizes synchronized console output so worker lines cannot interleave.
class OutputManager {
public:
    // Prints one complete line while holding the console mutex.
    void print_line(const std::string& line); 

    // Prints a discovered prime with its worker ID aand timestamp.
    void print_prime(const PrimeResult& result);

private:
    // Protects std::cout from concurrent worker output.
    std::mutex console_mutex_;
};

// Polymorphic base class for the two workload-division strategies.
class WorkDivisionStrategy {
public:
    virtual ~WorkDivisionStrategy() = default;

    // Executes the selected workloa strategy and returns run statistics.
    [[nodiscard]] virtual RunStatistics run(const Config& config, OutputManager& output) = 0;
};

// Coordinates configuration execution, output, and strategy selection.
class PrimeCheckerApp {
public: 
    int run(const Config& config);

public:
    OutputManager output_;
};

// Performs a normal single-threaded primality test.
[[nodiscard]] bool is_prime_sequential(std::uint64_t n);

// Converts a wall-clock timestamp into a readable string.
[[nodiscard]] std::string format_timestamp(const std::chrono::system_clock::time_point& time_point);

}; // namespace primechecker