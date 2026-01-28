// Resource Manager Unit Tests
//
// Tests cover:
// - Resource configuration (set, remove, query)
// - Allocation calculation (fair share)
// - Dynamic rebalancing when clients join/leave
// - Edge cases

#include "resource_manager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <memory>

namespace throttling {
namespace {

using namespace std::chrono_literals;

// =============================================================================
// Test Fixture
// =============================================================================

class ResourceManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    clock_ = std::make_shared<FakeClock>();
    registry_ = std::make_unique<ClientRegistry>(30s, clock_);
    manager_ = std::make_unique<ResourceManager>(registry_.get());
  }

  // Helper to register a client and set resource interests
  void RegisterClientWithInterests(const std::string& client_id,
                                   const std::set<int64_t>& resources) {
    registry_->RegisterClient(client_id);
    registry_->Heartbeat(client_id, resources);
  }

  std::shared_ptr<FakeClock> clock_;
  std::unique_ptr<ClientRegistry> registry_;
  std::unique_ptr<ResourceManager> manager_;
};

// =============================================================================
// Resource Configuration Tests
// =============================================================================

TEST_F(ResourceManagerTest, SetResourceLimit) {
  EXPECT_TRUE(manager_->SetResourceLimit(1, 100.0));
  EXPECT_TRUE(manager_->HasResource(1));
}

TEST_F(ResourceManagerTest, SetResourceLimitZero) {
  EXPECT_TRUE(manager_->SetResourceLimit(1, 0.0));
  EXPECT_TRUE(manager_->HasResource(1));

  auto info = manager_->GetResourceInfo(1);
  ASSERT_TRUE(info.has_value());
  EXPECT_DOUBLE_EQ(info->rate_limit, 0.0);
}

TEST_F(ResourceManagerTest, SetResourceLimitNegativeFails) {
  EXPECT_FALSE(manager_->SetResourceLimit(1, -10.0));
  EXPECT_FALSE(manager_->HasResource(1));
}

TEST_F(ResourceManagerTest, SetResourceLimitUpdatesExisting) {
  manager_->SetResourceLimit(1, 100.0);
  manager_->SetResourceLimit(1, 200.0);

  auto info = manager_->GetResourceInfo(1);
  ASSERT_TRUE(info.has_value());
  EXPECT_DOUBLE_EQ(info->rate_limit, 200.0);
}

TEST_F(ResourceManagerTest, RemoveResource) {
  manager_->SetResourceLimit(1, 100.0);
  EXPECT_TRUE(manager_->HasResource(1));

  EXPECT_TRUE(manager_->RemoveResource(1));
  EXPECT_FALSE(manager_->HasResource(1));
}

TEST_F(ResourceManagerTest, RemoveNonexistentResourceFails) {
  EXPECT_FALSE(manager_->RemoveResource(999));
}

TEST_F(ResourceManagerTest, GetResourceInfoNotFound) {
  auto info = manager_->GetResourceInfo(999);
  EXPECT_FALSE(info.has_value());
}

TEST_F(ResourceManagerTest, GetResourceInfoWithClients) {
  manager_->SetResourceLimit(1, 100.0);
  RegisterClientWithInterests("client-1", {1});
  RegisterClientWithInterests("client-2", {1});

  auto info = manager_->GetResourceInfo(1);
  ASSERT_TRUE(info.has_value());
  EXPECT_DOUBLE_EQ(info->rate_limit, 100.0);
  EXPECT_EQ(info->active_client_count, 2);
}

TEST_F(ResourceManagerTest, HasResourceFalseForUnconfigured) {
  EXPECT_FALSE(manager_->HasResource(1));
}

// =============================================================================
// Allocation Tests
// =============================================================================

TEST_F(ResourceManagerTest, AllocationSingleClient) {
  manager_->SetResourceLimit(1, 100.0);
  RegisterClientWithInterests("client-1", {1});

  double alloc = manager_->GetAllocation("client-1", 1);
  EXPECT_DOUBLE_EQ(alloc, 100.0);  // Full rate for single client
}

TEST_F(ResourceManagerTest, AllocationTwoClients) {
  manager_->SetResourceLimit(1, 100.0);
  RegisterClientWithInterests("client-1", {1});
  RegisterClientWithInterests("client-2", {1});

  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 50.0);
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-2", 1), 50.0);
}

TEST_F(ResourceManagerTest, AllocationThreeClients) {
  manager_->SetResourceLimit(1, 99.0);  // Divisible by 3
  RegisterClientWithInterests("client-1", {1});
  RegisterClientWithInterests("client-2", {1});
  RegisterClientWithInterests("client-3", {1});

  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 33.0);
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-2", 1), 33.0);
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-3", 1), 33.0);
}

TEST_F(ResourceManagerTest, AllocationFractional) {
  manager_->SetResourceLimit(1, 100.0);
  RegisterClientWithInterests("client-1", {1});
  RegisterClientWithInterests("client-2", {1});
  RegisterClientWithInterests("client-3", {1});

  // 100 / 3 = 33.333...
  double expected = 100.0 / 3.0;
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), expected);
}

TEST_F(ResourceManagerTest, AllocationUnconfiguredResource) {
  RegisterClientWithInterests("client-1", {1});

  // Resource 1 not configured
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 0.0);
}

TEST_F(ResourceManagerTest, AllocationUninterestedClient) {
  manager_->SetResourceLimit(1, 100.0);
  RegisterClientWithInterests("client-1", {2});  // Interested in resource 2, not 1

  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 0.0);
}

TEST_F(ResourceManagerTest, AllocationUnregisteredClient) {
  manager_->SetResourceLimit(1, 100.0);
  RegisterClientWithInterests("client-1", {1});

  // Unknown client
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("unknown", 1), 0.0);
}

TEST_F(ResourceManagerTest, AllocationZeroRateLimit) {
  manager_->SetResourceLimit(1, 0.0);
  RegisterClientWithInterests("client-1", {1});

  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 0.0);
}

// =============================================================================
// Dynamic Rebalancing Tests
// =============================================================================

TEST_F(ResourceManagerTest, AllocationRebalancesWhenClientJoins) {
  manager_->SetResourceLimit(1, 100.0);
  RegisterClientWithInterests("client-1", {1});

  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 100.0);

  // Second client joins
  RegisterClientWithInterests("client-2", {1});

  // Allocation automatically rebalances
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 50.0);
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-2", 1), 50.0);
}

TEST_F(ResourceManagerTest, AllocationRebalancesWhenClientLeaves) {
  manager_->SetResourceLimit(1, 100.0);
  RegisterClientWithInterests("client-1", {1});
  RegisterClientWithInterests("client-2", {1});

  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 50.0);

  // Client-2 unregisters
  registry_->UnregisterClient("client-2");

  // Allocation automatically rebalances
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 100.0);
}

TEST_F(ResourceManagerTest, AllocationRebalancesWhenInterestChanges) {
  manager_->SetResourceLimit(1, 100.0);
  RegisterClientWithInterests("client-1", {1});
  RegisterClientWithInterests("client-2", {1});

  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 50.0);

  // Client-2 loses interest in resource 1
  registry_->Heartbeat("client-2", {2});  // Now interested in 2, not 1

  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 100.0);
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-2", 1), 0.0);
}

// =============================================================================
// GetAllAllocations Tests
// =============================================================================

TEST_F(ResourceManagerTest, GetAllAllocationsEmpty) {
  RegisterClientWithInterests("client-1", {});

  auto allocs = manager_->GetAllAllocations("client-1");
  EXPECT_TRUE(allocs.empty());
}

TEST_F(ResourceManagerTest, GetAllAllocationsMultipleResources) {
  manager_->SetResourceLimit(1, 100.0);
  manager_->SetResourceLimit(2, 200.0);
  manager_->SetResourceLimit(3, 300.0);

  RegisterClientWithInterests("client-1", {1, 2, 3});

  auto allocs = manager_->GetAllAllocations("client-1");
  EXPECT_EQ(allocs.size(), 3);
  EXPECT_DOUBLE_EQ(allocs[1], 100.0);
  EXPECT_DOUBLE_EQ(allocs[2], 200.0);
  EXPECT_DOUBLE_EQ(allocs[3], 300.0);
}

TEST_F(ResourceManagerTest, GetAllAllocationsWithSharing) {
  manager_->SetResourceLimit(1, 100.0);
  manager_->SetResourceLimit(2, 200.0);

  RegisterClientWithInterests("client-1", {1, 2});
  RegisterClientWithInterests("client-2", {1});  // Only interested in resource 1

  auto allocs = manager_->GetAllAllocations("client-1");
  EXPECT_DOUBLE_EQ(allocs[1], 50.0);   // Shared with client-2
  EXPECT_DOUBLE_EQ(allocs[2], 200.0);  // Not shared
}

TEST_F(ResourceManagerTest, GetAllAllocationsUnconfiguredResource) {
  manager_->SetResourceLimit(1, 100.0);
  // Resource 2 not configured

  RegisterClientWithInterests("client-1", {1, 2});

  auto allocs = manager_->GetAllAllocations("client-1");
  EXPECT_EQ(allocs.size(), 2);
  EXPECT_DOUBLE_EQ(allocs[1], 100.0);
  EXPECT_DOUBLE_EQ(allocs[2], 0.0);  // Unconfigured resource
}

TEST_F(ResourceManagerTest, GetAllAllocationsUnknownClient) {
  manager_->SetResourceLimit(1, 100.0);

  auto allocs = manager_->GetAllAllocations("unknown");
  EXPECT_TRUE(allocs.empty());
}

// =============================================================================
// Multiple Resources Tests
// =============================================================================

TEST_F(ResourceManagerTest, MultipleResourcesIndependent) {
  manager_->SetResourceLimit(1, 100.0);
  manager_->SetResourceLimit(2, 200.0);

  RegisterClientWithInterests("client-1", {1});
  RegisterClientWithInterests("client-2", {2});

  // Each gets full allocation on their resource
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 100.0);
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-2", 2), 200.0);

  // Cross-resource: not interested
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 2), 0.0);
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-2", 1), 0.0);
}

// =============================================================================
// Listener Interface Tests (should be no-ops)
// =============================================================================

TEST_F(ResourceManagerTest, ListenerCallbacksAreNoOps) {
  // Just verify they don't crash
  manager_->OnClientJoined("client-1");
  manager_->OnClientLeft("client-1", {1, 2});
  manager_->OnResourceInterestChanged("client-1", {1}, {2});
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(ResourceManagerTest, VerySmallRateLimit) {
  manager_->SetResourceLimit(1, 0.001);  // Very small
  RegisterClientWithInterests("client-1", {1});

  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 0.001);
}

TEST_F(ResourceManagerTest, VeryLargeRateLimit) {
  manager_->SetResourceLimit(1, 1e12);  // Very large
  RegisterClientWithInterests("client-1", {1});
  RegisterClientWithInterests("client-2", {1});

  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-1", 1), 5e11);
}

TEST_F(ResourceManagerTest, ManyClients) {
  manager_->SetResourceLimit(1, 1000.0);

  // Register 100 clients
  for (int i = 0; i < 100; ++i) {
    RegisterClientWithInterests("client-" + std::to_string(i), {1});
  }

  // Each should get 10.0
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-0", 1), 10.0);
  EXPECT_DOUBLE_EQ(manager_->GetAllocation("client-99", 1), 10.0);
}

}  // namespace
}  // namespace throttling
