// Throttling Service - Main Application
//
// Story:
// Entry point for the throttling server. Initializes all components, starts the
// gRPC server, and handles graceful shutdown on SIGINT/SIGTERM.
//
// Usage:
//   ./throttling_server [--port=PORT] [--heartbeat-timeout=SECONDS]
//
// Options:
//   --port=PORT              Server port (default: 50051)
//   --heartbeat-timeout=SEC  Client heartbeat timeout (default: 30)
//   --help                   Show usage

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "client_registry.h"
#include "resource_manager.h"
#include "throttling_server.h"

namespace {

// =============================================================================
// Configuration
// =============================================================================

struct ServerConfig {
  uint16_t port = 50051;
  std::chrono::seconds heartbeat_timeout{30};
};

// =============================================================================
// Global State (for signal handling)
// =============================================================================

std::atomic<bool> g_shutdown_requested{false};
std::mutex g_shutdown_mutex;
std::condition_variable g_shutdown_cv;

// =============================================================================
// Signal Handler
// =============================================================================

void SignalHandler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    g_shutdown_requested.store(true);
    g_shutdown_cv.notify_all();
  }
}

// =============================================================================
// Command-Line Parsing
// =============================================================================

void PrintUsage(const char* program_name) {
  std::cout << "Usage: " << program_name << " [OPTIONS]\n"
            << "\n"
            << "Options:\n"
            << "  --port=PORT              Server port (default: 50051)\n"
            << "  --heartbeat-timeout=SEC  Client heartbeat timeout in seconds "
               "(default: 30)\n"
            << "  --help                   Show this help message\n";
}

std::optional<ServerConfig> ParseArgs(int argc, char* argv[]) {
  ServerConfig config;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return std::nullopt;
    }

    if (arg.rfind("--port=", 0) == 0) {
      std::string value = arg.substr(7);
      try {
        int port = std::stoi(value);
        if (port <= 0 || port > 65535) {
          std::cerr << "Error: Invalid port number: " << value << std::endl;
          return std::nullopt;
        }
        config.port = static_cast<uint16_t>(port);
      } catch (const std::exception& e) {
        std::cerr << "Error: Invalid port number: " << value << std::endl;
        return std::nullopt;
      }
      continue;
    }

    if (arg.rfind("--heartbeat-timeout=", 0) == 0) {
      std::string value = arg.substr(20);
      try {
        int timeout = std::stoi(value);
        if (timeout < 0) {
          std::cerr << "Error: Invalid heartbeat timeout: " << value
                    << std::endl;
          return std::nullopt;
        }
        config.heartbeat_timeout = std::chrono::seconds(timeout);
      } catch (const std::exception& e) {
        std::cerr << "Error: Invalid heartbeat timeout: " << value << std::endl;
        return std::nullopt;
      }
      continue;
    }

    std::cerr << "Error: Unknown argument: " << arg << std::endl;
    PrintUsage(argv[0]);
    return std::nullopt;
  }

  return config;
}

// =============================================================================
// Server Runner
// =============================================================================

int RunServer(const ServerConfig& config) {
  // Build server address
  std::string server_address = "0.0.0.0:" + std::to_string(config.port);

  // Create components
  throttling::ClientRegistry client_registry(config.heartbeat_timeout);
  throttling::ResourceManager resource_manager(&client_registry);

  // Register ResourceManager as listener for client events
  client_registry.SetListener(&resource_manager);

  // Create service implementation
  throttling::ThrottlingServiceImpl service(&client_registry, &resource_manager);

  // Build and start server
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  if (!server) {
    std::cerr << "Error: Failed to start server on " << server_address
              << std::endl;
    return 1;
  }

  std::cout << "Throttling server started on " << server_address << std::endl;
  std::cout << "Heartbeat timeout: " << config.heartbeat_timeout.count()
            << " seconds" << std::endl;
  std::cout << "Press Ctrl+C to shutdown..." << std::endl;

  // Start a thread that waits for shutdown signal and calls server->Shutdown()
  std::thread shutdown_thread([&server]() {
    std::unique_lock<std::mutex> lock(g_shutdown_mutex);
    g_shutdown_cv.wait(lock, []() { return g_shutdown_requested.load(); });

    std::cout << "\nShutdown requested, stopping server..." << std::endl;
    server->Shutdown();
  });

  // Wait for server to finish (will unblock when Shutdown() is called)
  server->Wait();

  // Wait for shutdown thread to complete
  shutdown_thread.join();

  std::cout << "Server shutdown complete." << std::endl;

  return 0;
}

}  // namespace

// =============================================================================
// Main Entry Point
// =============================================================================

int main(int argc, char* argv[]) {
  // Parse command-line arguments
  auto config = ParseArgs(argc, argv);
  if (!config) {
    return 1;
  }

  // Setup signal handlers
  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  // Run the server
  return RunServer(*config);
}
