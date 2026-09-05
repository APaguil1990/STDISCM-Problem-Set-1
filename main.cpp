#include "prime_checker.hpp"

#include <exception> 
#include <iostream> 

int main() {
    try {
        // Load and fully validate the configuration before starting the application. 
        const primechecker::Config config = primechecker::ConfigLoader::load_from_file("config.txt"); 

        // Run the prime checker using the validated configuration. 
        primechecker::PrimeCheckerApp app; 
        return app.run(config);
    } catch (const primechecker::ConfigError& error) {
        // Handle configuration-related errors with a clear termination message. 
        std::cerr << "Error: " << error.what() << '\n'; 

        if (error.kind() == primechecker::ConfigError::Kind::MissingRequiredField) {
            std::cerr << "Program terminated because config.txt is incomplete.\n";
        } else {
            std::cerr << "Program terminated because the configuration is invalid.\n";
        }

        return 1;
    } catch (const std::exception& error) {
        // Handle unexpected standard exceptions and exit safely. 
        std::cerr << "Error: " << error.what() << '\n';
        std::cerr << "Program terminated safely.\n"; 

        return 1;
    }
}