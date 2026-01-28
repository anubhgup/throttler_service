// Client Registry - Implementation
//
// See client_registry.h for the Story and algorithm description.

#include "client_registry.h"

#include <algorithm>
#include <vector>

namespace throttling {

ClientRegistry::ClientRegistry(std::chrono::seconds heartbeat_timeout,
                               std::shared_ptr<Clock> clock)
    : heartbeat_timeout_(heartbeat_timeout),
      clock_(clock ? std::move(clock) : std::make_shared<RealClock>()) {}

void ClientRegistry::SetListener(ClientRegistryListener* listener) {
  std::lock_guard<std::mutex> lock(mutex_);
  listener_ = listener;
}

bool ClientRegistry::RegisterClient(const std::string& client_id) {
  if (client_id.empty()) {
    return false;
  }

  ExpireStaleClients();

  std::lock_guard<std::mutex> lock(mutex_);

  auto it = clients_.find(client_id);
  if (it != clients_.end()) {
    // Existing client - update heartbeat time (idempotent)
    it->second.last_heartbeat_time = clock_->Now();
    return true;
  }

  // New client
  ClientInfo info;
  info.last_heartbeat_time = clock_->Now();
  clients_[client_id] = info;

  // Notify listener
  if (listener_) {
    listener_->OnClientJoined(client_id);
  }

  return true;
}

bool ClientRegistry::Heartbeat(const std::string& client_id,
                               const std::set<int64_t>& resource_ids) {
  ExpireStaleClients();

  std::lock_guard<std::mutex> lock(mutex_);

  auto it = clients_.find(client_id);
  if (it == clients_.end()) {
    return false;  // Client not registered
  }

  // Update heartbeat time
  it->second.last_heartbeat_time = clock_->Now();

  // Compute resource interest changes
  const auto& old_resources = it->second.interested_resources;
  std::set<int64_t> added_resources;
  std::set<int64_t> removed_resources;

  // Find added resources (in new but not in old)
  std::set_difference(resource_ids.begin(), resource_ids.end(),
                      old_resources.begin(), old_resources.end(),
                      std::inserter(added_resources, added_resources.begin()));

  // Find removed resources (in old but not in new)
  std::set_difference(old_resources.begin(), old_resources.end(),
                      resource_ids.begin(), resource_ids.end(),
                      std::inserter(removed_resources, removed_resources.begin()));

  // Update resource_to_clients_ index
  for (int64_t resource_id : added_resources) {
    resource_to_clients_[resource_id].insert(client_id);
  }
  for (int64_t resource_id : removed_resources) {
    auto& clients_set = resource_to_clients_[resource_id];
    clients_set.erase(client_id);
    if (clients_set.empty()) {
      resource_to_clients_.erase(resource_id);
    }
  }

  // Update client's interested resources
  it->second.interested_resources = resource_ids;

  // Notify listener if interests changed
  if (listener_ && (!added_resources.empty() || !removed_resources.empty())) {
    listener_->OnResourceInterestChanged(client_id, added_resources,
                                         removed_resources);
  }

  return true;
}

bool ClientRegistry::UnregisterClient(const std::string& client_id) {
  ExpireStaleClients();

  std::lock_guard<std::mutex> lock(mutex_);

  auto it = clients_.find(client_id);
  if (it == clients_.end()) {
    return true;  // Idempotent - client already gone
  }

  RemoveClientLocked(client_id);
  return true;
}

std::set<std::string> ClientRegistry::GetClientsForResource(
    int64_t resource_id) const {
  ExpireStaleClients();

  std::lock_guard<std::mutex> lock(mutex_);

  auto it = resource_to_clients_.find(resource_id);
  if (it == resource_to_clients_.end()) {
    return {};
  }
  return it->second;
}

std::set<int64_t> ClientRegistry::GetInterestedResources(
    const std::string& client_id) const {
  ExpireStaleClients();

  std::lock_guard<std::mutex> lock(mutex_);

  auto it = clients_.find(client_id);
  if (it == clients_.end()) {
    return {};
  }
  return it->second.interested_resources;
}

size_t ClientRegistry::GetClientCount() const {
  ExpireStaleClients();

  std::lock_guard<std::mutex> lock(mutex_);
  return clients_.size();
}

bool ClientRegistry::IsClientRegistered(const std::string& client_id) const {
  ExpireStaleClients();

  std::lock_guard<std::mutex> lock(mutex_);
  return clients_.find(client_id) != clients_.end();
}

void ClientRegistry::ExpireStaleClients() const {
  std::lock_guard<std::mutex> lock(mutex_);

  // Collect stale clients (can't modify map while iterating)
  std::vector<std::string> stale_clients;
  for (const auto& [client_id, info] : clients_) {
    if (IsClientStaleLocked(info)) {
      stale_clients.push_back(client_id);
    }
  }

  // Remove stale clients
  // Note: We need to cast away const for RemoveClientLocked
  // This is safe because clients_ and resource_to_clients_ are mutable
  auto* mutable_this = const_cast<ClientRegistry*>(this);
  for (const auto& client_id : stale_clients) {
    mutable_this->RemoveClientLocked(client_id);
  }
}

bool ClientRegistry::IsClientStaleLocked(const ClientInfo& info) const {
  auto now = clock_->Now();
  auto elapsed = now - info.last_heartbeat_time;
  // Compare in the same unit for precision
  return elapsed > heartbeat_timeout_;
}

void ClientRegistry::RemoveClientLocked(const std::string& client_id) {
  auto it = clients_.find(client_id);
  if (it == clients_.end()) {
    return;
  }

  // Save interested resources for notification
  std::set<int64_t> interested_resources = it->second.interested_resources;

  // Remove from resource_to_clients_ index
  for (int64_t resource_id : interested_resources) {
    auto& clients_set = resource_to_clients_[resource_id];
    clients_set.erase(client_id);
    if (clients_set.empty()) {
      resource_to_clients_.erase(resource_id);
    }
  }

  // Remove from clients_
  clients_.erase(it);

  // Notify listener
  if (listener_) {
    listener_->OnClientLeft(client_id, interested_resources);
  }
}

}  // namespace throttling
