// Client Registry
//
// Story:
// This module tracks connected clients and their liveness. It manages client
// registration, heartbeat tracking, and lazy expiration of stale clients.
// A listener interface allows the ResourceManager to be notified of changes.
//
// Algorithm:
// - Clients register and send periodic heartbeats with resource interests
// - Stale clients (no heartbeat within timeout) are lazily expired during operations
// - Reverse index maintained for efficient "clients for resource" queries
// - Listener notified of client join/leave and resource interest changes
//
// Thread Safety:
// All public methods are thread-safe (protected by mutex).

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "token_bucket.h"  // For Clock interface

namespace throttling {

/// Listener interface for client registry events.
///
/// Implement this interface to receive notifications when clients
/// join, leave, or change their resource interests.
class ClientRegistryListener {
 public:
  virtual ~ClientRegistryListener() = default;

  /// Called when a new client registers.
  /// @param client_id The ID of the new client.
  virtual void OnClientJoined(const std::string& client_id) = 0;

  /// Called when a client leaves (explicit unregister or expired).
  /// @param client_id The ID of the departing client.
  /// @param interested_resources Resources the client was interested in.
  virtual void OnClientLeft(const std::string& client_id,
                            const std::set<int64_t>& interested_resources) = 0;

  /// Called when a client's resource interests change.
  /// @param client_id The client whose interests changed.
  /// @param added_resources Newly added resource interests.
  /// @param removed_resources Removed resource interests.
  virtual void OnResourceInterestChanged(
      const std::string& client_id,
      const std::set<int64_t>& added_resources,
      const std::set<int64_t>& removed_resources) = 0;
};

/// Information about a registered client.
struct ClientInfo {
  std::chrono::steady_clock::time_point last_heartbeat_time;
  std::set<int64_t> interested_resources;
};

/// Tracks connected clients and their liveness.
///
/// Manages client registration, heartbeats, and resource interests.
/// Performs lazy expiration of stale clients during operations.
///
/// Example:
///   ClientRegistry registry(std::chrono::seconds(30));
///   registry.SetListener(&my_listener);
///   registry.RegisterClient("client-1");
///   registry.Heartbeat("client-1", {1, 2, 3});
class ClientRegistry {
 public:
  /// Constructs a client registry.
  ///
  /// @param heartbeat_timeout Duration after which a client is considered stale.
  /// @param clock Clock implementation for time. If nullptr, uses RealClock.
  explicit ClientRegistry(
      std::chrono::seconds heartbeat_timeout = std::chrono::seconds(30),
      std::shared_ptr<Clock> clock = nullptr);

  ~ClientRegistry() = default;

  // Non-copyable, non-movable
  ClientRegistry(const ClientRegistry&) = delete;
  ClientRegistry& operator=(const ClientRegistry&) = delete;

  /// Sets the listener for client events.
  /// @param listener Listener to receive events, or nullptr to disable.
  void SetListener(ClientRegistryListener* listener);

  /// Registers a client.
  ///
  /// Idempotent: re-registering updates the heartbeat time.
  ///
  /// @param client_id Unique identifier for the client.
  /// @return true on success, false if client_id is empty.
  bool RegisterClient(const std::string& client_id);

  /// Processes a heartbeat from a client.
  ///
  /// Updates the client's heartbeat time and resource interests.
  /// Notifies the listener of any interest changes.
  ///
  /// @param client_id The client sending the heartbeat.
  /// @param resource_ids Resources this client is interested in.
  /// @return true on success, false if client is not registered.
  bool Heartbeat(const std::string& client_id,
                 const std::set<int64_t>& resource_ids);

  /// Unregisters a client.
  ///
  /// Idempotent: unregistering an unknown client returns success.
  ///
  /// @param client_id The client to unregister.
  /// @return true always.
  bool UnregisterClient(const std::string& client_id);

  /// Returns the set of clients interested in a resource.
  /// @param resource_id The resource to query.
  /// @return Set of client IDs interested in this resource.
  std::set<std::string> GetClientsForResource(int64_t resource_id) const;

  /// Returns the resources a client is interested in.
  /// @param client_id The client to query.
  /// @return Set of resource IDs, or empty if client not found.
  std::set<int64_t> GetInterestedResources(const std::string& client_id) const;

  /// Returns the number of registered (non-expired) clients.
  size_t GetClientCount() const;

  /// Checks if a client is registered (and not expired).
  bool IsClientRegistered(const std::string& client_id) const;

 private:
  /// Expires stale clients. Called during operations.
  void ExpireStaleClients() const;

  /// Checks if a client is stale. Must hold mutex_.
  bool IsClientStaleLocked(const ClientInfo& info) const;

  /// Removes a client and notifies listener. Must hold mutex_.
  void RemoveClientLocked(const std::string& client_id);

  // Configuration
  std::chrono::seconds heartbeat_timeout_;
  std::shared_ptr<Clock> clock_;

  // Client data (mutable for lazy expiration in const methods)
  mutable std::map<std::string, ClientInfo> clients_;
  mutable std::map<int64_t, std::set<std::string>> resource_to_clients_;

  // Listener (not owned, may be nullptr)
  ClientRegistryListener* listener_ = nullptr;

  // Thread safety
  mutable std::mutex mutex_;
};

}  // namespace throttling
