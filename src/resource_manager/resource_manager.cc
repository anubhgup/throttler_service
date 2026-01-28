// Resource Manager - Implementation
//
// See resource_manager.h for the Story and algorithm description.

#include "resource_manager.h"

namespace throttling {

ResourceManager::ResourceManager(ClientRegistry* client_registry)
    : client_registry_(client_registry) {}

bool ResourceManager::SetResourceLimit(int64_t resource_id, double rate_limit) {
  if (rate_limit < 0.0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  resource_limits_[resource_id] = rate_limit;
  return true;
}

bool ResourceManager::RemoveResource(int64_t resource_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return resource_limits_.erase(resource_id) > 0;
}

std::optional<ResourceInfo> ResourceManager::GetResourceInfo(
    int64_t resource_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = resource_limits_.find(resource_id);
  if (it == resource_limits_.end()) {
    return std::nullopt;
  }

  ResourceInfo info;
  info.rate_limit = it->second;
  info.active_client_count =
      client_registry_->GetClientsForResource(resource_id).size();
  return info;
}

bool ResourceManager::HasResource(int64_t resource_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return resource_limits_.find(resource_id) != resource_limits_.end();
}

double ResourceManager::GetAllocation(const std::string& client_id,
                                      int64_t resource_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if resource is configured
  auto it = resource_limits_.find(resource_id);
  if (it == resource_limits_.end()) {
    return 0.0;
  }

  double rate_limit = it->second;

  // Get interested clients
  auto interested_clients =
      client_registry_->GetClientsForResource(resource_id);

  // Check if this client is interested
  if (interested_clients.find(client_id) == interested_clients.end()) {
    return 0.0;
  }

  // Calculate fair share
  size_t num_clients = interested_clients.size();
  if (num_clients == 0) {
    return 0.0;  // Defensive - shouldn't happen if client is in set
  }

  return rate_limit / static_cast<double>(num_clients);
}

std::map<int64_t, double> ResourceManager::GetAllAllocations(
    const std::string& client_id) const {
  std::map<int64_t, double> allocations;

  // Get resources this client is interested in
  auto interested_resources =
      client_registry_->GetInterestedResources(client_id);

  // Calculate allocation for each resource
  for (int64_t resource_id : interested_resources) {
    double allocation = GetAllocation(client_id, resource_id);
    allocations[resource_id] = allocation;
  }

  return allocations;
}

// ===========================================================================
// ClientRegistryListener callbacks - all no-ops
// Allocations are calculated dynamically, so no state updates needed.
// ===========================================================================

void ResourceManager::OnClientJoined(const std::string& /*client_id*/) {
  // No-op: client has no resource interests yet
}

void ResourceManager::OnClientLeft(
    const std::string& /*client_id*/,
    const std::set<int64_t>& /*interested_resources*/) {
  // No-op: allocations recalculated dynamically on next query
}

void ResourceManager::OnResourceInterestChanged(
    const std::string& /*client_id*/,
    const std::set<int64_t>& /*added_resources*/,
    const std::set<int64_t>& /*removed_resources*/) {
  // No-op: allocations recalculated dynamically on next query
}

}  // namespace throttling
