// Client Registry Unit Tests
//
// Tests cover:
// - Registration and unregistration
// - Heartbeat and resource interest tracking
// - Lazy expiration of stale clients
// - Listener notifications
// - Query operations
// - Edge cases

#include "client_registry.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace throttling {
namespace {

using namespace std::chrono_literals;

// =============================================================================
// Mock Listener for testing
// =============================================================================

class MockListener : public ClientRegistryListener {
 public:
  void OnClientJoined(const std::string& client_id) override {
    joined_clients.push_back(client_id);
  }

  void OnClientLeft(const std::string& client_id,
                    const std::set<int64_t>& interested_resources) override {
    left_clients.push_back({client_id, interested_resources});
  }

  void OnResourceInterestChanged(
      const std::string& client_id,
      const std::set<int64_t>& added_resources,
      const std::set<int64_t>& removed_resources) override {
    interest_changes.push_back({client_id, added_resources, removed_resources});
  }

  void Clear() {
    joined_clients.clear();
    left_clients.clear();
    interest_changes.clear();
  }

  std::vector<std::string> joined_clients;
  std::vector<std::pair<std::string, std::set<int64_t>>> left_clients;
  std::vector<std::tuple<std::string, std::set<int64_t>, std::set<int64_t>>>
      interest_changes;
};

// =============================================================================
// Test Fixture
// =============================================================================

class ClientRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    clock_ = std::make_shared<FakeClock>();
    registry_ = std::make_unique<ClientRegistry>(30s, clock_);
    registry_->SetListener(&listener_);
  }

  std::shared_ptr<FakeClock> clock_;
  std::unique_ptr<ClientRegistry> registry_;
  MockListener listener_;
};

// =============================================================================
// Registration Tests
// =============================================================================

TEST_F(ClientRegistryTest, RegisterNewClient) {
  EXPECT_TRUE(registry_->RegisterClient("client-1"));
  EXPECT_TRUE(registry_->IsClientRegistered("client-1"));
  EXPECT_EQ(registry_->GetClientCount(), 1);

  ASSERT_EQ(listener_.joined_clients.size(), 1);
  EXPECT_EQ(listener_.joined_clients[0], "client-1");
}

TEST_F(ClientRegistryTest, RegisterMultipleClients) {
  EXPECT_TRUE(registry_->RegisterClient("client-1"));
  EXPECT_TRUE(registry_->RegisterClient("client-2"));
  EXPECT_TRUE(registry_->RegisterClient("client-3"));

  EXPECT_EQ(registry_->GetClientCount(), 3);
  EXPECT_EQ(listener_.joined_clients.size(), 3);
}

TEST_F(ClientRegistryTest, RegisterSameClientTwiceIsIdempotent) {
  EXPECT_TRUE(registry_->RegisterClient("client-1"));
  EXPECT_TRUE(registry_->RegisterClient("client-1"));  // Re-register

  EXPECT_EQ(registry_->GetClientCount(), 1);
  // Only one join notification
  EXPECT_EQ(listener_.joined_clients.size(), 1);
}

TEST_F(ClientRegistryTest, RegisterEmptyClientIdFails) {
  EXPECT_FALSE(registry_->RegisterClient(""));
  EXPECT_EQ(registry_->GetClientCount(), 0);
  EXPECT_TRUE(listener_.joined_clients.empty());
}

// =============================================================================
// Unregistration Tests
// =============================================================================

TEST_F(ClientRegistryTest, UnregisterClient) {
  registry_->RegisterClient("client-1");
  listener_.Clear();

  EXPECT_TRUE(registry_->UnregisterClient("client-1"));
  EXPECT_FALSE(registry_->IsClientRegistered("client-1"));
  EXPECT_EQ(registry_->GetClientCount(), 0);

  ASSERT_EQ(listener_.left_clients.size(), 1);
  EXPECT_EQ(listener_.left_clients[0].first, "client-1");
}

TEST_F(ClientRegistryTest, UnregisterClientWithResources) {
  registry_->RegisterClient("client-1");
  registry_->Heartbeat("client-1", {1, 2, 3});
  listener_.Clear();

  registry_->UnregisterClient("client-1");

  ASSERT_EQ(listener_.left_clients.size(), 1);
  EXPECT_EQ(listener_.left_clients[0].first, "client-1");
  EXPECT_EQ(listener_.left_clients[0].second, std::set<int64_t>({1, 2, 3}));

  // Reverse index should be cleaned up
  EXPECT_TRUE(registry_->GetClientsForResource(1).empty());
  EXPECT_TRUE(registry_->GetClientsForResource(2).empty());
  EXPECT_TRUE(registry_->GetClientsForResource(3).empty());
}

TEST_F(ClientRegistryTest, UnregisterUnknownClientIsIdempotent) {
  EXPECT_TRUE(registry_->UnregisterClient("unknown"));
  EXPECT_TRUE(listener_.left_clients.empty());
}

// =============================================================================
// Heartbeat Tests
// =============================================================================

TEST_F(ClientRegistryTest, HeartbeatUpdatesTime) {
  registry_->RegisterClient("client-1");

  clock_->Advance(10s);
  EXPECT_TRUE(registry_->Heartbeat("client-1", {}));

  // Client should still be registered (heartbeat renewed)
  clock_->Advance(25s);  // 35s total, but only 25s since heartbeat
  EXPECT_TRUE(registry_->IsClientRegistered("client-1"));
}

TEST_F(ClientRegistryTest, HeartbeatFailsForUnregisteredClient) {
  EXPECT_FALSE(registry_->Heartbeat("unknown", {1, 2}));
}

TEST_F(ClientRegistryTest, HeartbeatAddsResourceInterests) {
  registry_->RegisterClient("client-1");
  listener_.Clear();

  EXPECT_TRUE(registry_->Heartbeat("client-1", {1, 2, 3}));

  EXPECT_EQ(registry_->GetInterestedResources("client-1"),
            std::set<int64_t>({1, 2, 3}));

  // Check reverse index
  EXPECT_EQ(registry_->GetClientsForResource(1),
            std::set<std::string>({"client-1"}));
  EXPECT_EQ(registry_->GetClientsForResource(2),
            std::set<std::string>({"client-1"}));

  // Check listener notification
  ASSERT_EQ(listener_.interest_changes.size(), 1);
  auto& [client_id, added, removed] = listener_.interest_changes[0];
  EXPECT_EQ(client_id, "client-1");
  EXPECT_EQ(added, std::set<int64_t>({1, 2, 3}));
  EXPECT_TRUE(removed.empty());
}

TEST_F(ClientRegistryTest, HeartbeatRemovesResourceInterests) {
  registry_->RegisterClient("client-1");
  registry_->Heartbeat("client-1", {1, 2, 3});
  listener_.Clear();

  EXPECT_TRUE(registry_->Heartbeat("client-1", {2}));  // Remove 1, 3

  EXPECT_EQ(registry_->GetInterestedResources("client-1"),
            std::set<int64_t>({2}));

  // Check reverse index cleaned up
  EXPECT_TRUE(registry_->GetClientsForResource(1).empty());
  EXPECT_TRUE(registry_->GetClientsForResource(3).empty());
  EXPECT_EQ(registry_->GetClientsForResource(2),
            std::set<std::string>({"client-1"}));

  // Check listener notification
  ASSERT_EQ(listener_.interest_changes.size(), 1);
  auto& [client_id, added, removed] = listener_.interest_changes[0];
  EXPECT_EQ(client_id, "client-1");
  EXPECT_TRUE(added.empty());
  EXPECT_EQ(removed, std::set<int64_t>({1, 3}));
}

TEST_F(ClientRegistryTest, HeartbeatChangesResourceInterests) {
  registry_->RegisterClient("client-1");
  registry_->Heartbeat("client-1", {1, 2});
  listener_.Clear();

  EXPECT_TRUE(registry_->Heartbeat("client-1", {2, 3}));  // Remove 1, add 3

  EXPECT_EQ(registry_->GetInterestedResources("client-1"),
            std::set<int64_t>({2, 3}));

  ASSERT_EQ(listener_.interest_changes.size(), 1);
  auto& [client_id, added, removed] = listener_.interest_changes[0];
  EXPECT_EQ(added, std::set<int64_t>({3}));
  EXPECT_EQ(removed, std::set<int64_t>({1}));
}

TEST_F(ClientRegistryTest, HeartbeatNoChangeNoNotification) {
  registry_->RegisterClient("client-1");
  registry_->Heartbeat("client-1", {1, 2});
  listener_.Clear();

  EXPECT_TRUE(registry_->Heartbeat("client-1", {1, 2}));  // Same interests

  EXPECT_TRUE(listener_.interest_changes.empty());
}

// =============================================================================
// Expiration Tests
// =============================================================================

TEST_F(ClientRegistryTest, StaleClientExpired) {
  registry_->RegisterClient("client-1");
  listener_.Clear();

  clock_->Advance(31s);  // Past 30s timeout

  // Any operation triggers expiration
  EXPECT_EQ(registry_->GetClientCount(), 0);
  EXPECT_FALSE(registry_->IsClientRegistered("client-1"));

  ASSERT_EQ(listener_.left_clients.size(), 1);
  EXPECT_EQ(listener_.left_clients[0].first, "client-1");
}

TEST_F(ClientRegistryTest, HeartbeatPreventsExpiration) {
  registry_->RegisterClient("client-1");

  clock_->Advance(20s);
  registry_->Heartbeat("client-1", {});

  clock_->Advance(20s);  // 40s total, but only 20s since heartbeat
  EXPECT_TRUE(registry_->IsClientRegistered("client-1"));
}

TEST_F(ClientRegistryTest, MultipleClientsExpireIndependently) {
  registry_->RegisterClient("client-1");
  clock_->Advance(10s);
  registry_->RegisterClient("client-2");
  clock_->Advance(10s);
  registry_->RegisterClient("client-3");
  listener_.Clear();

  clock_->Advance(15s);  // 35s for client-1, 25s for client-2, 15s for client-3

  EXPECT_EQ(registry_->GetClientCount(), 2);  // client-1 expired
  EXPECT_FALSE(registry_->IsClientRegistered("client-1"));
  EXPECT_TRUE(registry_->IsClientRegistered("client-2"));
  EXPECT_TRUE(registry_->IsClientRegistered("client-3"));
}

TEST_F(ClientRegistryTest, ExpiredClientResourcesCleanedUp) {
  registry_->RegisterClient("client-1");
  registry_->Heartbeat("client-1", {1, 2});
  listener_.Clear();

  clock_->Advance(31s);

  // Trigger expiration
  registry_->GetClientCount();

  EXPECT_TRUE(registry_->GetClientsForResource(1).empty());
  EXPECT_TRUE(registry_->GetClientsForResource(2).empty());

  ASSERT_EQ(listener_.left_clients.size(), 1);
  EXPECT_EQ(listener_.left_clients[0].second, std::set<int64_t>({1, 2}));
}

// =============================================================================
// Query Tests
// =============================================================================

TEST_F(ClientRegistryTest, GetClientsForResourceMultipleClients) {
  registry_->RegisterClient("client-1");
  registry_->RegisterClient("client-2");
  registry_->RegisterClient("client-3");

  registry_->Heartbeat("client-1", {1, 2});
  registry_->Heartbeat("client-2", {2, 3});
  registry_->Heartbeat("client-3", {1, 3});

  EXPECT_EQ(registry_->GetClientsForResource(1),
            std::set<std::string>({"client-1", "client-3"}));
  EXPECT_EQ(registry_->GetClientsForResource(2),
            std::set<std::string>({"client-1", "client-2"}));
  EXPECT_EQ(registry_->GetClientsForResource(3),
            std::set<std::string>({"client-2", "client-3"}));
  EXPECT_TRUE(registry_->GetClientsForResource(99).empty());
}

TEST_F(ClientRegistryTest, GetInterestedResourcesUnknownClient) {
  EXPECT_TRUE(registry_->GetInterestedResources("unknown").empty());
}

TEST_F(ClientRegistryTest, IsClientRegisteredAfterExpiration) {
  registry_->RegisterClient("client-1");
  clock_->Advance(31s);

  EXPECT_FALSE(registry_->IsClientRegistered("client-1"));
}

// =============================================================================
// Listener Tests
// =============================================================================

TEST_F(ClientRegistryTest, NullListenerDoesNotCrash) {
  registry_->SetListener(nullptr);

  EXPECT_TRUE(registry_->RegisterClient("client-1"));
  EXPECT_TRUE(registry_->Heartbeat("client-1", {1, 2}));
  EXPECT_TRUE(registry_->UnregisterClient("client-1"));
}

TEST_F(ClientRegistryTest, ChangeListenerMidOperation) {
  registry_->RegisterClient("client-1");

  MockListener new_listener;
  registry_->SetListener(&new_listener);

  registry_->RegisterClient("client-2");

  EXPECT_EQ(listener_.joined_clients.size(), 1);  // Only client-1
  EXPECT_EQ(new_listener.joined_clients.size(), 1);  // Only client-2
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(ClientRegistryTest, ZeroTimeoutExpiresImmediately) {
  auto fast_registry = std::make_unique<ClientRegistry>(0s, clock_);
  MockListener fast_listener;
  fast_registry->SetListener(&fast_listener);

  fast_registry->RegisterClient("client-1");

  clock_->Advance(1ms);  // Any time passes

  EXPECT_EQ(fast_registry->GetClientCount(), 0);
}

TEST_F(ClientRegistryTest, EmptyResourceSet) {
  registry_->RegisterClient("client-1");
  registry_->Heartbeat("client-1", {1, 2});
  listener_.Clear();

  registry_->Heartbeat("client-1", {});  // Clear all interests

  EXPECT_TRUE(registry_->GetInterestedResources("client-1").empty());
  EXPECT_TRUE(registry_->GetClientsForResource(1).empty());
  EXPECT_TRUE(registry_->GetClientsForResource(2).empty());

  ASSERT_EQ(listener_.interest_changes.size(), 1);
  auto& [client_id, added, removed] = listener_.interest_changes[0];
  EXPECT_TRUE(added.empty());
  EXPECT_EQ(removed, std::set<int64_t>({1, 2}));
}

TEST_F(ClientRegistryTest, DefaultClockWorks) {
  // Test with real clock (no FakeClock injection)
  auto real_registry = std::make_unique<ClientRegistry>(30s);

  EXPECT_TRUE(real_registry->RegisterClient("client-1"));
  EXPECT_TRUE(real_registry->IsClientRegistered("client-1"));
}

}  // namespace
}  // namespace throttling
