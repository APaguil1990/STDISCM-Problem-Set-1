# Multithreading Prime Number Checker 

A C++20 application that demonstrates **multithreading**, **concurrency**, **synchronization**, and **workload partitioning** by finding prime numbers using configurable thread counts across four variants: **A1-B1**, **A1-B2**, **A2-B1** and **A2-B2**.

The project uses an object-oriented design with separate configuration, output, application, and workload-strategy components.

## Objective 
Showcase how different concurrency strategies affect prime-number checking and program performance by comparing: 
- **Printing Variant**:
    - **A1 (immediate)** - worker threads print prime results as soon as they are discovered.
    - **A2 (batch)** - worker threads store prime results locally and print them after computation.
- **Division Scheme**:
    - **B1 (range division)** - divides the candidate-number range into contiguous sections and assigns each section to a worker. 
    - **B2 (divisibility testing)** - multiple persistent workers cooperate on the **same candidate number** by testing different divisors in parallel.
 
Each run reports useful information such as:
* selected variant;
* requested and active worker counts;
* prime values;
* logical thread IDs;
* timestamps;
* total primes found;
* prime computation time;
* total program execution time.

## Variants 
The program supports all four combinations:
* **A1-B1** - immediate printing with candidate-range division.
* **A2-B1** - batch printing with candidate-range division.
* **A1-B2** - immediate pritning with parallel divisor testing. 
* **A2-B2** - batch printing with parallel divisor testing.

## B1 - Range Division
B1 divides the candidate values from `2` through the configured maximum into **contiguous ranges**.

Example: 
```text
Thread 0 -> 2-25
Thread 1 -> 26-50 
Thread 2 -> 51-75
Thread 3 -> 76-100
```

Each worker independently performs a sequential primality test on the numbers in its assigned range. The number of active workers is capped when more threads are requested than there are candidate values. 

## B2 - Divisibility Division 
B2 parallelizes the **divisor-testing work for each candidate number** instead of assigning different candidate numbers to different workers.

The worker threads are created once and remain active throughout the B2 computation. This avoids creating a new set of operating-system threads for every candidate.

For example, with several active workers:
```text
Candidate: 97

Thread 0 -> divisors 3, 17, 31, ...
Thread 1 -> divisors 5, 19, 33, ...
Thread 2 -> divisors 7, 21, 35, ...
Thread 3 -> divisors 9, 23, 37, ...
```

Each worker receives a different lane of odd divisors. 

Workers only test divisors while
```text
divisor <= candidate / divisor
```

This avoids floating-point square-root inaccuracies and multiplication overflow. 

### Early Composite Detection
B2 uses an atomic flag to notify workers when a factor has been found.

For example:
```text
Thread 1 | Candidate: 75 | Factor found: 5 | Canceling remaining divisor checks
```

Once one worker proves that the candidate is composite, the other workers stop unnecessary divisor checks as soon as they observe the shared atomic flag.

The cancellation is cooperative, so another worker may already have started or printed its activity before observing the cancellation request.

### A1 - Immediate Printing

A1 prints a prime immediately when it is discovered.

Example:

```text
Thread  2 | Prime:       79 | Time: 2026-09-06 10:45:46.340
```

Console output is protected by a mutex so multiple workers cannot corrupt or interleave individual output lines.

Because printing happens during computation, console I/O and mutex contention are intentionally part of the A1 workload.

### A2 - Batch Printing

A2 avoids printing primes during the main computation.

Each worker stores its results in a **thread-local result vector**, preventing repeated locking of a shared global result container.

After all workers finish, the results are merged, sorted, and printed.

This reduces console and synchronization overhead during prime computation.

## Object-Oriented Design

The application uses several classes and structures to separate responsibilities:

* **`Config`** - stores validated program configuration.
* **`ConfigLoader`** - reads and validates `config.txt`.
* **`ConfigError`** - represents configuration-related errors.
* **`PrimeResult`** - stores a prime, discovery timestamp, and logical worker ID.
* **`RunStatistics`** - stores execution statistics and batch results.
* **`OutputManager`** - provides synchronized console output.
* **`WorkDivisionStrategy`** - polymorphic base class for workload strategies.
* **`RangeDivisionStrategy`** - implements B1.
* **`DivisibilityDivisionStrategy`** - implements B2.
* **`DivisorWorkerPool`** - manages reusable B2 worker threads.
* **`PrimeCheckerApp`** - coordinates strategy selection, execution, output, and timing.

This allows B1 and B2 to share a common interface while keeping their implementations separate.

## Quick Start 
Compile using MinGW/GCC:
```text
g++ -std=c++20 -O3 -pthread main.cpp prime_check.cpp -o prime_checker.exe
```

Run on Windows:
```text
prime_checker.exe
```

On Linux:
```text
./prime_checker
```

The program expects `config.txt` to be available in the current working directory.

## Configuration 
Create or edit `config.txt`:
```text
# Number of requested worker threads
Threads = 20

# Maximum candidate value
Max Value = 2^8

# A1 = immediate printing
# A2 = batch printing
Printing Variant = A1

# B1 = candidate range division
# B2 = parallel divisor testing
Division Scheme = B1

# Optional B2 diagnostic output
Verbose Divisibility = false
```

The original four-line configuration remains valid:
```text
Threads = 20
Max Value = 2^8
Printing Variant = A1
Division Scheme = A1
```

If `Verbose Divisibility` is omitted, it defaults to:
```text
false
```

## Configuration Validation
The following settings are required:
- `Threads`
- `Max Value`
- `Printing Variant` 
- `Division scheme`

The configuration loader validates all settings before any worker threads are started.

Validation includes:

* `Threads` must be greater than `0`.
* `Max Value` must be at least `2`.
* decimal maximum values are supported.
* `2^n` notation is supported for valid `uint64_t` exponents.
* `Printing Variant` must be exactly `A1` or `A2`.
* `Division Scheme` must be exactly `B1` or `B2`.
* `Verbose Divisibility` must be `true` or `false` when provided.
* duplicate configuration keys are rejected.
* unknown configuration keys are rejected.
* malformed lines are reported.
* blank lines and lines beginning with `#` are ignored.

If required configuration is missing or invalid, the program exits safely **before starting worker threads or performing prime checking**.

Example:

```text
Error: Missing required configuration: Division Scheme
Program terminated because config.txt is incomplete.
```

## Verbose Divisibility Logging

When using B2, detailed divisor activity can be enabled with:

```text
Verbose Divisibility = true
```

Example:

```text
Thread  0 | Candidate: 157 | Checking odd divisors from 3 with stride 14 (while d <= n/d)
Thread  1 | Candidate: 159 | Factor found: 5 | Canceling remaining divisor checks
```

The **stride** represents the distance between divisors assigned to the same worker.

For example, with 7 active B2 workers:

```text
stride = 2 * 7 = 14
```

Only odd divisors are tested, so workers receive separate divisor sequences without duplicating each other's assigned divisor positions.

Verbose logging is intended for **demonstration and debugging**.

It should normally be disabled during performance testing because synchronized console output can significantly increase execution time.

## Thread Management

The program uses simple logical worker IDs:

```text
Thread 0
Thread 1
Thread 2
...
```

instead of implementation-specific `std::thread::id` values.

B1 creates workers for the useful candidate ranges.

B2 creates a reusable set of persistent `std::jthread` workers. These workers wait for candidate-number rounds, perform their assigned divisor work, synchronize when the round is complete, and then process the next candidate.

All workers are stopped and joined cleanly before the program exits.

## Synchronization

The project demonstrates several C++20 concurrency techniques:

* **`std::jthread`** for RAII-managed worker threads;
* **`std::mutex`** for shared state and synchronized console output;
* **`std::condition_variable` / `std::condition_variable_any`** for coordinating persistent B2 workers;
* **`std::atomic`** for prime counts and early composite detection;
* **per-thread vectors** for low-contention A2 result collection;
* **cooperative cancellation** to stop unnecessary B2 divisor checks.

Expensive primality calculations are performed outside console and shared-state mutex critical sections whenever possible.

## Timing

The program separates human-readable timestamps from benchmark timing.

* **`std::chrono::system_clock`** is used for prime discovery and program timestamps.
* **`std::chrono::steady_clock`** is used for elapsed execution time.

Example:

```text
Prime computation time: 12431 us (12.431 ms)
Total program time: 19822 us (19.822 ms)
```

For **A1**, immediate console printing is part of the measured computation workload.

For **A2**, batch result printing occurs after the prime-computation interval, although it is still included in total program time.

## Performance Notes

Performance depends on factors such as:

* processor core count;
* requested thread count;
* maximum candidate value;
* operating-system scheduling;
* console performance;
* synchronization overhead;
* selected A/B strategy.

B1 typically has lower synchronization overhead because workers independently process candidate ranges, although contiguous ranges may produce some workload imbalance.

B2 demonstrates a different form of parallelism by having workers cooperate on the divisibility checks of the same candidate. For small numbers, the synchronization required between candidate rounds may cost more than simply performing a sequential primality test.

A1 is generally affected heavily by synchronized console I/O.

A2 reduces this overhead by collecting results locally and printing them after computation.

No variant is guaranteed to be fastest on every machine or input size.

## Project Purpose

This project is designed primarily as an educational demonstration of:

* multithreading;
* concurrency;
* parallelism;
* workload partitioning;
* mutual exclusion;
* atomic synchronization;
* persistent worker threads;
* batching;
* cooperative early cancellation;
* safe configuration handling;
* object-oriented strategy design.

The four variants provide a controlled way to observe how different printing and work-division strategies affect the behavior and performance of a concurrent C++20 application.

## Developed by: 
- James Archer B. Paguiligan

## Demo video link: 
- https://drive.google.com/file/d/1oCLqifsxuJdq_07thpsHAlY-XsqmT2Xl/view?usp=sharing