#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <map>
#include <algorithm>
#include <future>
#include <cctype>

// Trim whitespace from string
std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

// Config structure
struct Config {
    int num_threads;
    uint64_t max_value;
    std::string printing_variant; // A1 or A2
    std::string division_scheme;  // B1 or B2

    void print() const {
        std::cout << "\nConfiguration: " << std::endl;
        std::cout << " Threads: " << num_threads << std::endl;
        std::cout << " Max Value: " << max_value << std::endl;
        std::cout << " Printing Variant: " << printing_variant << std::endl;
        std::cout << " Division Scheme: " << division_scheme << std::endl;
    }
};

// Result structure for prime numbers - FIX: Store logical thread_id instead of system thread::id
struct PrimeResult {
    uint64_t prime;
    std::chrono::system_clock::time_point timestamp;
    int thread_id;  // Store logical thread ID instead of system thread ID
};

// Global variables for synchronization
std::mutex cout_mutex;
std::mutex results_mutex;
std::vector<PrimeResult> global_results;

// Function to read configuration from file
Config read_config(const std::string& filename) {
    Config config;
    std::ifstream file(filename);

    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filename);
    }

    std::string line;
    int lines_processed = 0;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue; // Skip empty lines and comments

        size_t equals_pos = line.find('=');

        if (equals_pos == std::string::npos) {
            std::cerr << "Warning: Invalid line format (missing '='): " << line << std::endl;
            continue;
        }

        std::string key = trim(line.substr(0, equals_pos));
        std::string value = trim(line.substr(equals_pos + 1));

        if (key == "Threads") {
            config.num_threads = std::stoi(value);
        } else if (key == "Max Value") {
            if (value.find("2^") == 0) {
                int exponent = std::stoi(value.substr(2));
                config.max_value = (exponent == 64) ? UINT64_MAX : (1ULL << exponent);
            } else {
                config.max_value = std::stoull(value);
            }
        } else if (key == "Printing Variant") {
            config.printing_variant = value;
        } else if (key == "Division Scheme") {
            config.division_scheme = value;
        } else {
            std::cerr << "Warning: Unknown key: " << key << std::endl;
        }
        lines_processed++;
    }

    if (lines_processed == 0) {
        throw std::runtime_error("No valid configuration found in file.");
    }

    return config;
}

// Check if number is prime
bool is_prime(uint64_t n) {
    if (n < 2) return false;
    if (n == 2) return true;
    if (n % 2 == 0) return false;

    uint64_t sqrt_n = static_cast<uint64_t>(std::sqrt(n));

    for (uint64_t i = 3; i <= sqrt_n; i += 2) {
        if (n % i == 0) return false;
    }
    return true;
}

// Format timestamp
std::string format_timestamp(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// B1: Straight division of search range
void search_primes_b1(uint64_t start, uint64_t end, int thread_id, const std::string& printing_variant) {
    auto thread_start = std::chrono::system_clock::now();

    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "\nThread " << std::setw(2) << thread_id << " started at: " << format_timestamp(thread_start) 
                  << " (Range: " << start << "-" << end << ")" << std::endl;
    }

    std::vector<PrimeResult> local_results;

    for (uint64_t n = start; n <= end; ++n) {
        if (is_prime(n)) {
            auto timestamp = std::chrono::system_clock::now();

            if (printing_variant == "A1") {
                // Print immediately
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "Thread " << std::setw(2) << thread_id 
                          << " | Prime: " << std::setw(8) << n 
                          << " | Time: " << format_timestamp(timestamp) << std::endl;
            } else {
                // Store for later print - FIX: Store logical thread_id
                local_results.push_back({n, timestamp, thread_id});
            }
        }
    }

    // For A2, store results in global vector
    if (printing_variant == "A2") {
        std::lock_guard<std::mutex> lock(results_mutex);
        global_results.insert(global_results.end(), local_results.begin(), local_results.end());
    }

    auto thread_end = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "Thread " << std::setw(2) << thread_id << " finished at: " << format_timestamp(thread_end) << std::endl;
}

// B2: Linear search with divisibility testing - FIXED: Ensure all threads get work
void search_primes_b2(uint64_t max_value, int num_threads, int thread_id, const std::string& printing_variant) {
    auto thread_start = std::chrono::system_clock::now();

    std::vector<PrimeResult> local_results;
    uint64_t numbers_processed = 0;
    uint64_t primes_found = 0;

    // Each thread tests numbers where (number - 2) % num_threads == thread_id
    // This ensures all threads get approximately equal work
    for (uint64_t n = 2 + thread_id; n <= max_value; n += num_threads) {
        numbers_processed++;

        if (is_prime(n)) {
            primes_found++;
            auto timestamp = std::chrono::system_clock::now();

            if (printing_variant == "A1") {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "Thread " << std::setw(2) << thread_id 
                          << " | Prime: " << std::setw(8) << n 
                          << " | Time: " << format_timestamp(timestamp) << std::endl;
            } else {
                local_results.push_back({n, timestamp, thread_id});
            }
        }
    }

    if (printing_variant == "A2") {
        std::lock_guard<std::mutex> lock(results_mutex);
        global_results.insert(global_results.end(), local_results.begin(), local_results.end());
    }

    auto thread_end = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "Thread " << std::setw(2) << thread_id << " finished at: " << format_timestamp(thread_end)
              << " (Processed: " << numbers_processed << " numbers, Found: " << primes_found
              << " primes)" << std::endl;
} 

// A1-B1: Immediate printing with straight division
void run_a1_b1(const Config& config) {
    std::cout << "\n=== Running Variant A1-B1 ===" << std::endl;
    std::cout << "Threads: " << config.num_threads << ", Max Value: " << config.max_value << std::endl;

    auto start_time = std::chrono::system_clock::now();
    std::cout << "Program started at: " << format_timestamp(start_time) << "\n" << std::endl;

    std::vector<std::thread> threads;
    uint64_t range_size = config.max_value / config.num_threads;

    for (int i = 0; i < config.num_threads; ++i) {
        uint64_t start = (i == 0) ? 2 : (i * range_size + 1);
        uint64_t end = (i == config.num_threads - 1) ? config.max_value : ((i + 1) * range_size);
        threads.emplace_back(search_primes_b1, start, end, i, config.printing_variant);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "\nProgram finished at: " << format_timestamp(end_time) << std::endl;
    std::cout << "Total execution time: " << duration.count() << " ms" << std::endl;
}

// A1-B2: Immediate printing with divisibility testing
void run_a1_b2(const Config& config) {
    std::cout << "\n=== Running Variant A1-B2 ===" << std::endl;
    std::cout << "Threads: " << config.num_threads << ", Max Value: " << config.max_value << std::endl;

    auto start_time = std::chrono::system_clock::now();
    std::cout << "Program started at: " << format_timestamp(start_time) << "\n" << std::endl;

    std::vector<std::thread> threads;

    for (int i = 0; i < config.num_threads; ++i) {
        threads.emplace_back(search_primes_b2, config.max_value, config.num_threads, i, config.printing_variant);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "\nProgram finished at: " << format_timestamp(end_time) << std::endl;
    std::cout << "Total execution time: " << duration.count() << " ms" << std::endl;
}

// A2-B1: Batch printing with straight division - FIXED: No need for thread_id_map
void run_a2_b1(const Config& config) {
    std::cout << "\n=== Running Variant A2-B1 ===" << std::endl;
    std::cout << "Threads: " << config.num_threads << ", Max Value: " << config.max_value << std::endl;

    global_results.clear();
    auto start_time = std::chrono::system_clock::now();
    std::cout << "Program started at: " << format_timestamp(start_time) << std::endl;

    std::vector<std::thread> threads;
    uint64_t range_size = config.max_value / config.num_threads;

    for (int i = 0; i < config.num_threads; ++i) {
        uint64_t start = (i == 0) ? 2 : (i * range_size + 1);
        uint64_t end = (i == config.num_threads - 1) ? config.max_value : ((i + 1) * range_size);
        threads.emplace_back(search_primes_b1, start, end, i, config.printing_variant);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Print results all at once - FIX: Directly use stored thread_id
    std::cout << "\n=== Batch Results ===" << std::endl;
    std::cout << "Total primes found: " << global_results.size() << std::endl;
    
    // Sort results by prime number for better readability
    std::sort(global_results.begin(), global_results.end(), 
              [](const PrimeResult& a, const PrimeResult& b) { return a.prime < b.prime; });

    for (const auto& result : global_results) {
        std::cout << "Thread " << std::setw(2) << result.thread_id 
                  << " | Prime: " << std::setw(8) << result.prime 
                  << " | Time: " << format_timestamp(result.timestamp) << std::endl;
    }

    auto end_time = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "\nProgram finished at: " << format_timestamp(end_time) << std::endl;
    std::cout << "Total execution time: " << duration.count() << " ms" << std::endl;
}

// A2-B2: Batch printing with divisibility testing - MODIFIED: Compact thread-sorted version
void run_a2_b2(const Config& config) {
    std::cout << "\n=== Running Variant A2-B2 ===" << std::endl;
    std::cout << "Threads: " << config.num_threads << ", Max Value: " << config.max_value << std::endl;

    global_results.clear();
    auto start_time = std::chrono::system_clock::now();
    std::cout << "Program started at: " << format_timestamp(start_time) << "\n" << std::endl;

    std::vector<std::thread> threads;

    for (int i = 0; i < config.num_threads; ++i) {
        threads.emplace_back(search_primes_b2, config.max_value, config.num_threads, i, config.printing_variant);
    }

    for (auto& t : threads) {
        t.join();
    }

    // Print results grouped by thread ID in ascending order
    std::cout << "\n=== Batch Results (Sorted by Thread ID) ===" << std::endl;
    std::cout << "Total primes found: " << global_results.size() << std::endl;
    
    // Group and sort by thread ID
    std::map<int, std::vector<PrimeResult>> results_by_thread;
    for (const auto& result : global_results) {
        results_by_thread[result.thread_id].push_back(result);
    }

    // Display all results sorted by thread ID, with primes sorted within each thread
    for (const auto& [thread_id, primes] : results_by_thread) {
        // Create a sorted copy of primes for this thread
        std::vector<PrimeResult> sorted_primes = primes;
        std::sort(sorted_primes.begin(), sorted_primes.end(),
                  [](const PrimeResult& a, const PrimeResult& b) { return a.prime < b.prime; });
        
        for (const auto& result : sorted_primes) {
            std::cout << "Thread " << std::setw(2) << thread_id 
                      << " | Prime: " << std::setw(8) << result.prime
                      << " | Time: " << format_timestamp(result.timestamp) << std::endl;
        }
    }

    // Summary by thread 
    std::cout << "\n=== Summary by Thread ===" << std::endl;
    std::map<int, std::vector<uint64_t>> primes_by_thread;
    for (const auto& result : global_results) {
        primes_by_thread[result.thread_id].push_back(result.prime);
    }

    for (const auto& [thread_id, primes] : primes_by_thread) {
        std::cout << "Thread " << thread_id << " found " << primes.size() << " primes: ";
        // Show first few primes for each thread
        int show_count = std::min(5, static_cast<int>(primes.size()));
        for (int i = 0; i < show_count; ++i) {
            std::cout << primes[i];
            if (i < show_count - 1) std::cout << ", ";
        }
        if (primes.size() > show_count) {
            std::cout << ", ... and " << (primes.size() - show_count) << " more";
        }
        std::cout << std::endl;
    }

    auto end_time = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "\nProgram finished at: " << format_timestamp(end_time) << std::endl;
    std::cout << "Total execution time: " << duration.count() << " ms" << std::endl;
}

int main() {
    try {
        // Read config file
        Config config = read_config("config.txt");
        config.print();

        // Execute variant
        if (config.printing_variant == "A1" && config.division_scheme == "B1") {
            std::cout << "Selected variant: A1-B1" << std::endl;
            run_a1_b1(config);
        } else if (config.printing_variant == "A1" && config.division_scheme == "B2") {
            std::cout << "Selected variant: A1-B2" << std::endl;
            run_a1_b2(config);
        } else if (config.printing_variant == "A2" && config.division_scheme == "B1") {
            std::cout << "Selected variant: A2-B1" << std::endl;
            run_a2_b1(config);
        } else if (config.printing_variant == "A2" && config.division_scheme == "B2") {
            std::cout << "Selected variant: A2-B2" << std::endl;
            run_a2_b2(config);
        } else {
            std::cerr << "Invalid configuration variant." << std::endl;
            std::cerr << "Printing variant must be A1 or A2; division scheme must be B1 or B2" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}