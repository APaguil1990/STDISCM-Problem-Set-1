# Multithreading Prime Number Checker 

A C++20 application that demonstrates multithreading performance by finding prime numbers using configurable thread counts and division schemes across four variants (A1-B1, A1-B2, A2-B1, A2-B2). 

## Objective 
Showcase the impact of concurrency strategies on primality checking throughput by testing 
- **Printing Variant**:
    - **A1 (immediate)** - threads print results as they find them
    - **A2 (batch)** - threads collect results and print in batches
- **Division Scheme**:
    - **B1 (range division)** - split numeric range across threads
    - **B2 (divisibility testing)** - split candidate divisors / testing work
 
Each run reports primes along with **thread IDs** and **timestamps** to visualize scheduling and throughput. 

## Quick Start 
- Compile: **g++ -std=c++20 -O3 -pthread prime_check.cpp -o prime_checker.exe
- Run: Create **config.txt** then execute **prime_checker**

## Configuration 
Edit **config.txt** with: 
- **Threads=4** (number of threads)
- **Max Value=1000** (or **2^n** for powers)
- **Printing Variant=A1** (A1=immediate, A2=batch)
- **Division Scheme=B1** (B1=range division, B2=divisibility testing)

The program reads the **config.txt** file, executes the printing variant and division scheme, and outputs the prime values with the thread IDs and timestamps. 
