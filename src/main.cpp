#include "core/Server.hpp"
#include "util/Config.hpp"
#include "util/Logger.hpp"
#include "util/Snowflake.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <string>

namespace {
    std::atomic<bool> g_running{true};
    chat::Server::Ptr g_server;

    void signalHandler(int signal) {
        if (signal == SIGINT || signal == SIGTERM) {
            LOG_INFO("Received shutdown signal ({})", signal);
            g_running = false;
            if (g_server) {
                g_server->stop();
            }
        }
    }

    void printUsage(const char* program) {
        std::cout << "Usage: " << program << " [options]\n"
                  << "\n"
                  << "Options:\n"
                  << "  --config <file>   Load configuration from file (default: config/server.json)\n"
                  << "  --host <host>     Server host (default: 0.0.0.0)\n"
                  << "  --port <port>     Server port (default: 7777)\n"
                  << "  --threads <n>     Number of IO threads (default: CPU cores)\n"
                  << "  --log-level <lvl> Log level: trace|debug|info|warn|error (default: info)\n"
                  << "  --machine-id <n>  Snowflake machine ID 0-1023 (default: 0)\n"
                  << "  --help            Show this help message\n"
                  << "\n"
                  << "Example:\n"
                  << "  " << program << " --config config/server.json\n"
                  << "  " << program << " --port 8080 --log-level debug\n"
                  << std::endl;
    }

    struct CommandLineArgs {
        std::string config_file = "config/server.json";
        std::string host;
        uint16_t port = 0;
        int threads = 0;
        std::string log_level;
        uint16_t machine_id = 0;
        bool help = false;
    };

    CommandLineArgs parseCommandLine(int argc, char* argv[]) {
        CommandLineArgs args;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--help" || arg == "-h") {
                args.help = true;
            }
            else if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
                args.config_file = argv[++i];
            }
            else if (arg == "--host" && i + 1 < argc) {
                args.host = argv[++i];
            }
            else if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
                args.port = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
            else if ((arg == "--threads" || arg == "-t") && i + 1 < argc) {
                args.threads = std::stoi(argv[++i]);
            }
            else if (arg == "--log-level" && i + 1 < argc) {
                args.log_level = argv[++i];
            }
            else if (arg == "--machine-id" && i + 1 < argc) {
                args.machine_id = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
        }

        return args;
    }
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    auto args = parseCommandLine(argc, argv);

    if (args.help) {
        printUsage(argv[0]);
        return 0;
    }

    try {
        // Load configuration
        chat::Config config;

        // Try to load from file
        if (!config.loadFromFile(args.config_file)) {
            std::cerr << "Warning: Could not load config file '" << args.config_file
                      << "', using defaults." << std::endl;
        }

        // Override with command line arguments
        if (!args.host.empty()) {
            config.host = args.host;
        }
        if (args.port > 0) {
            config.port = args.port;
        }
        if (args.threads > 0) {
            config.io_threads = args.threads;
        }
        if (!args.log_level.empty()) {
            config.log_level = args.log_level;
        }

        // Initialize logger
        chat::Logger::init(
            config.log_file,
            config.log_level,
            config.log_console,
            config.log_max_size_mb,
            config.log_max_files
        );

        // Initialize Snowflake ID generator
        chat::Snowflake::init(args.machine_id);

        LOG_INFO("===========================================");
        LOG_INFO("Universal Chat Server v1.0.0");
        LOG_INFO("===========================================");
        LOG_INFO("Configuration:");
        LOG_INFO("  Host:            {}", config.host);
        LOG_INFO("  Port:            {}", config.port);
        LOG_INFO("  IO Threads:      {}", config.io_threads);
        LOG_INFO("  Max Connections: {}", config.max_connections);
        LOG_INFO("  Log Level:       {}", config.log_level);
        LOG_INFO("  Machine ID:      {}", args.machine_id);
        LOG_INFO("===========================================");

        // Set up signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Create and start server
        g_server = chat::Server::create(config);
        g_server->start();

        LOG_INFO("Server is running. Press Ctrl+C to stop.");

        // Main loop
        while (g_running && g_server->isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // Optional: Print stats periodically
            // auto stats = g_server->getStats();
            // LOG_DEBUG("Connections: {} | Messages/sec: {}",
            //          stats.active_connections, stats.messages_per_second);
        }

        // Cleanup
        if (g_server) {
            g_server->stop();
            g_server.reset();
        }

        LOG_INFO("Server shutdown complete.");
        chat::Logger::shutdown();

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        LOG_CRITICAL("Fatal error: {}", e.what());
        return 1;
    }
}
