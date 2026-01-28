// Resource Manager
//
// Story:
// This module manages resources and calculates fair allocation of rate limits
// among interested clients. Allocations are calculated dynamically based on
// the number of interested clients - no caching, always consistent.
//
// Algorithm:
// - allocation = rate_limit / num_interested_clients
// - When clients join/leave, allocations automatically adjust on next query
// - ClientRegistryListener callbacks are no-ops (dynamic calculation)
//
// Thread Safety:
// All public methods are thread-safe (protected by mutex).

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>

#include "client_registry.h"

namespace throttling {

/// Information about a configured resource.
struct ResourceInfo {
  double rate_limit;           // Total RPS for this resource
  size_t active_client_count;  // Number of clients interested
};

/// Manages resources and calculates fair allocation of rate limits.
///
/// Allocations are calculated dynamically based on the number of interested
/// clients. When clients join/leave, allocations automatically adjust.
///
/// Example:
///   ResourceManager manager(&client_registry);
///   manager.SetResourceLimit(1, 100.0);  // 100 RPS for resource 1
///   // After 2 clients express interest in resource 1:
///   double alloc = manager.GetAllocation("client-1", 1);  // Returns 50.0
class ResourceManager : public ClientRegistryListener {
 public:
  /// Constructs a ResourceManager.
  ///
  /// @param client_registry Registry to query for interested clients.
  ///                        Must outlive this ResourceManager.
  explicit ResourceManager(ClientRegistry* client_registry);

  ~ResourceManager() = default;

  // Non-copyable, non-movable
  ResourceManager(const ResourceManager&) = delete;
  ResourceManager& operator=(const ResourceManager&) = delete;

  /// Sets the rate limit for a resource.
  ///
  /// Creates the resource if it doesn't exist, updates if it does.
  ///
  /// @param resource_id The resource to configure.
  /// @param rate_limit Total requests per second (must be >= 0).
  /// @return true on success, false if rate_limit < 0.
  bool SetResourceLimit(int64_t resource_id, double rate_limit);

  /// Removes a resource configuration.
  ///
  /// @param resource_id The resource to remove.
  /// @return true if removed, false if resource was not configured.
  bool RemoveResource(int64_t resource_id);

  /// Gets information about a resource.
  ///
  /// @param resource_id The resource to query.
  /// @return ResourceInfo if configured, std::nullopt otherwise.
  std::optional<ResourceInfo> GetResourceInfo(int64_t resource_id) const;

  /// Checks if a resource is configured.
  ///
  /// @param resource_id The resource to check.
  /// @return true if resource has a configured rate limit.
  bool HasResource(int64_t resource_id) const;

  /// Gets the allocation for a client on a resource.
  ///
  /// Calculates: rate_limit / num_interested_clients.
  /// Returns 0 if resource not configured or client not interested.
  ///
  /// @param client_id The client requesting allocation.
  /// @param resource_id The resource to query.
  /// @return Allocated rate (RPS) for this client, or 0.
  double GetAllocation(const std::string& client_id,
                       int64_t resource_id) const;

  /// Gets all allocations for a client.
  ///
  /// Returns allocations for all resources the client is interested in.
  /// Resources the client is interested in but not configured return 0.
  ///
  /// @param client_id The client to query.
  /// @return Map of resource_id -> allocated rate.
  std::map<int64_t, double> GetAllAllocations(const std::string& client_id) const;

  // =========================================================================
  // ClientRegistryListener interface
  // These are no-ops since allocations are calculated dynamically.
  // =========================================================================

  void OnClientJoined(const std::string& client_id) override;

  void OnClientLeft(const std::string& client_id,
                    const std::set<int64_t>& interested_resources) override;

  void OnResourceInterestChanged(
      const std::string& client_id,
      const std::set<int64_t>& added_resources,
      const std::set<int64_t>& removed_resources) override;

 private:
  // Dependency (not owned, must outlive this object)
  ClientRegistry* client_registry_;

  // Resource configuration: resource_id -> rate_limit
  std::map<int64_t, double> resource_limits_;

  // Thread safety
  mutable std::mutex mutex_;
};

}  // namespace throttling
