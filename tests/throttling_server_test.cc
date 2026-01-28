// Throttling Server Unit Tests
//
// Tests cover:
// - All gRPC handlers
// - Input validation
// - Error handling with gRPC status codes
// - Integration between ClientRegistry and ResourceManager

#include "throttling_server.h"

#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <memory>

namespace throttling {
namespace {

using namespace std::chrono_literals;

// =============================================================================
// Test Fixture
// =============================================================================

class ThrottlingServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    clock_ = std::make_shared<FakeClock>();
    registry_ = std::make_unique<ClientRegistry>(30s, clock_);
    manager_ = std::make_unique<ResourceManager>(registry_.get());
    service_ = std::make_unique<ThrottlingServiceImpl>(registry_.get(),
                                                       manager_.get());
  }

  std::shared_ptr<FakeClock> clock_;
  std::unique_ptr<ClientRegistry> registry_;
  std::unique_ptr<ResourceManager> manager_;
  std::unique_ptr<ThrottlingServiceImpl> service_;
};

// =============================================================================
// RegisterClient Tests
// =============================================================================

TEST_F(ThrottlingServerTest, RegisterClientSuccess) {
  RegisterClientRequest request;
  request.set_client_id("client-1");

  RegisterClientResponse response;
  grpc::Status status = service_->RegisterClient(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(response.success());
  EXPECT_TRUE(registry_->IsClientRegistered("client-1"));
}

TEST_F(ThrottlingServerTest, RegisterClientEmptyId) {
  RegisterClientRequest request;
  request.set_client_id("");

  RegisterClientResponse response;
  grpc::Status status = service_->RegisterClient(nullptr, &request, &response);

  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_FALSE(response.success());
  EXPECT_FALSE(response.error_message().empty());
}

TEST_F(ThrottlingServerTest, RegisterClientIdempotent) {
  RegisterClientRequest request;
  request.set_client_id("client-1");
  RegisterClientResponse response;

  service_->RegisterClient(nullptr, &request, &response);
  grpc::Status status = service_->RegisterClient(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(response.success());
  EXPECT_EQ(registry_->GetClientCount(), 1);
}

// =============================================================================
// UnregisterClient Tests
// =============================================================================

TEST_F(ThrottlingServerTest, UnregisterClientSuccess) {
  // First register
  registry_->RegisterClient("client-1");

  UnregisterClientRequest request;
  request.set_client_id("client-1");

  UnregisterClientResponse response;
  grpc::Status status = service_->UnregisterClient(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(response.success());
  EXPECT_FALSE(registry_->IsClientRegistered("client-1"));
}

TEST_F(ThrottlingServerTest, UnregisterClientIdempotent) {
  UnregisterClientRequest request;
  request.set_client_id("unknown");

  UnregisterClientResponse response;
  grpc::Status status = service_->UnregisterClient(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(response.success());
}

// =============================================================================
// Heartbeat Tests
// =============================================================================

TEST_F(ThrottlingServerTest, HeartbeatSuccess) {
  registry_->RegisterClient("client-1");
  manager_->SetResourceLimit(1, 100.0);
  manager_->SetResourceLimit(2, 200.0);

  HeartbeatRequest request;
  request.set_client_id("client-1");
  request.add_resource_ids(1);
  request.add_resource_ids(2);

  HeartbeatResponse response;
  grpc::Status status = service_->Heartbeat(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(response.success());

  // Check allocations returned
  const auto& allocations = response.allocations();
  EXPECT_EQ(allocations.size(), 2);
  EXPECT_DOUBLE_EQ(allocations.at(1), 100.0);
  EXPECT_DOUBLE_EQ(allocations.at(2), 200.0);
}

TEST_F(ThrottlingServerTest, HeartbeatEmptyClientId) {
  HeartbeatRequest request;
  request.set_client_id("");
  request.add_resource_ids(1);

  HeartbeatResponse response;
  grpc::Status status = service_->Heartbeat(nullptr, &request, &response);

  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_FALSE(response.success());
}

TEST_F(ThrottlingServerTest, HeartbeatClientNotRegistered) {
  HeartbeatRequest request;
  request.set_client_id("unknown");
  request.add_resource_ids(1);

  HeartbeatResponse response;
  grpc::Status status = service_->Heartbeat(nullptr, &request, &response);

  EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
  EXPECT_FALSE(response.success());
  EXPECT_EQ(response.error_message(), "client not registered");
}

TEST_F(ThrottlingServerTest, HeartbeatUpdatesResourceInterests) {
  registry_->RegisterClient("client-1");
  manager_->SetResourceLimit(1, 100.0);

  HeartbeatRequest request;
  request.set_client_id("client-1");
  request.add_resource_ids(1);

  HeartbeatResponse response;
  service_->Heartbeat(nullptr, &request, &response);

  EXPECT_EQ(registry_->GetInterestedResources("client-1"),
            std::set<int64_t>({1}));
}

TEST_F(ThrottlingServerTest, HeartbeatWithSharedResource) {
  registry_->RegisterClient("client-1");
  registry_->RegisterClient("client-2");
  registry_->Heartbeat("client-2", {1});  // client-2 interested in resource 1
  manager_->SetResourceLimit(1, 100.0);

  HeartbeatRequest request;
  request.set_client_id("client-1");
  request.add_resource_ids(1);

  HeartbeatResponse response;
  service_->Heartbeat(nullptr, &request, &response);

  // Two clients share resource 1, so each gets 50
  EXPECT_DOUBLE_EQ(response.allocations().at(1), 50.0);
}

// =============================================================================
// GetAllocation Tests
// =============================================================================

TEST_F(ThrottlingServerTest, GetAllocationSuccess) {
  registry_->RegisterClient("client-1");
  registry_->Heartbeat("client-1", {1});
  manager_->SetResourceLimit(1, 100.0);

  GetAllocationRequest request;
  request.set_client_id("client-1");
  request.set_resource_id(1);

  GetAllocationResponse response;
  grpc::Status status = service_->GetAllocation(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(response.success());
  EXPECT_DOUBLE_EQ(response.allocated_rate(), 100.0);
}

TEST_F(ThrottlingServerTest, GetAllocationEmptyClientId) {
  GetAllocationRequest request;
  request.set_client_id("");
  request.set_resource_id(1);

  GetAllocationResponse response;
  grpc::Status status = service_->GetAllocation(nullptr, &request, &response);

  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_FALSE(response.success());
}

TEST_F(ThrottlingServerTest, GetAllocationUnconfiguredResource) {
  registry_->RegisterClient("client-1");
  registry_->Heartbeat("client-1", {1});
  // Resource 1 not configured

  GetAllocationRequest request;
  request.set_client_id("client-1");
  request.set_resource_id(1);

  GetAllocationResponse response;
  grpc::Status status = service_->GetAllocation(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(response.success());
  EXPECT_DOUBLE_EQ(response.allocated_rate(), 0.0);  // Returns 0 for unconfigured
}

TEST_F(ThrottlingServerTest, GetAllocationUninterestedClient) {
  registry_->RegisterClient("client-1");
  registry_->Heartbeat("client-1", {2});  // Interested in resource 2
  manager_->SetResourceLimit(1, 100.0);

  GetAllocationRequest request;
  request.set_client_id("client-1");
  request.set_resource_id(1);  // Asking about resource 1

  GetAllocationResponse response;
  grpc::Status status = service_->GetAllocation(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(response.allocated_rate(), 0.0);  // Not interested
}

// =============================================================================
// SetResourceLimit Tests
// =============================================================================

TEST_F(ThrottlingServerTest, SetResourceLimitSuccess) {
  SetResourceLimitRequest request;
  request.set_resource_id(1);
  request.set_rate_limit(100.0);

  SetResourceLimitResponse response;
  grpc::Status status = service_->SetResourceLimit(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(response.success());
  EXPECT_TRUE(manager_->HasResource(1));
}

TEST_F(ThrottlingServerTest, SetResourceLimitZero) {
  SetResourceLimitRequest request;
  request.set_resource_id(1);
  request.set_rate_limit(0.0);

  SetResourceLimitResponse response;
  grpc::Status status = service_->SetResourceLimit(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(response.success());
}

TEST_F(ThrottlingServerTest, SetResourceLimitNegative) {
  SetResourceLimitRequest request;
  request.set_resource_id(1);
  request.set_rate_limit(-10.0);

  SetResourceLimitResponse response;
  grpc::Status status = service_->SetResourceLimit(nullptr, &request, &response);

  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_FALSE(response.success());
  EXPECT_EQ(response.error_message(), "rate_limit must be >= 0");
}

TEST_F(ThrottlingServerTest, SetResourceLimitUpdate) {
  manager_->SetResourceLimit(1, 100.0);

  SetResourceLimitRequest request;
  request.set_resource_id(1);
  request.set_rate_limit(200.0);

  SetResourceLimitResponse response;
  service_->SetResourceLimit(nullptr, &request, &response);

  auto info = manager_->GetResourceInfo(1);
  EXPECT_DOUBLE_EQ(info->rate_limit, 200.0);
}

// =============================================================================
// GetResourceLimit Tests
// =============================================================================

TEST_F(ThrottlingServerTest, GetResourceLimitSuccess) {
  manager_->SetResourceLimit(1, 100.0);
  registry_->RegisterClient("client-1");
  registry_->Heartbeat("client-1", {1});

  GetResourceLimitRequest request;
  request.set_resource_id(1);

  GetResourceLimitResponse response;
  grpc::Status status = service_->GetResourceLimit(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(response.success());
  EXPECT_DOUBLE_EQ(response.rate_limit(), 100.0);
  EXPECT_EQ(response.active_client_count(), 1);
}

TEST_F(ThrottlingServerTest, GetResourceLimitNotFound) {
  GetResourceLimitRequest request;
  request.set_resource_id(999);

  GetResourceLimitResponse response;
  grpc::Status status = service_->GetResourceLimit(nullptr, &request, &response);

  EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
  EXPECT_FALSE(response.success());
  EXPECT_EQ(response.error_message(), "resource not found");
}

TEST_F(ThrottlingServerTest, GetResourceLimitNoClients) {
  manager_->SetResourceLimit(1, 100.0);

  GetResourceLimitRequest request;
  request.set_resource_id(1);

  GetResourceLimitResponse response;
  grpc::Status status = service_->GetResourceLimit(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_DOUBLE_EQ(response.rate_limit(), 100.0);
  EXPECT_EQ(response.active_client_count(), 0);
}

// =============================================================================
// Integration Tests
// =============================================================================

TEST_F(ThrottlingServerTest, FullWorkflow) {
  // 1. Configure resource
  {
    SetResourceLimitRequest req;
    req.set_resource_id(1);
    req.set_rate_limit(100.0);
    SetResourceLimitResponse resp;
    service_->SetResourceLimit(nullptr, &req, &resp);
  }

  // 2. Register client
  {
    RegisterClientRequest req;
    req.set_client_id("client-1");
    RegisterClientResponse resp;
    service_->RegisterClient(nullptr, &req, &resp);
  }

  // 3. Heartbeat with resource interest
  {
    HeartbeatRequest req;
    req.set_client_id("client-1");
    req.add_resource_ids(1);
    HeartbeatResponse resp;
    service_->Heartbeat(nullptr, &req, &resp);

    EXPECT_DOUBLE_EQ(resp.allocations().at(1), 100.0);
  }

  // 4. Second client joins
  {
    RegisterClientRequest req;
    req.set_client_id("client-2");
    RegisterClientResponse resp;
    service_->RegisterClient(nullptr, &req, &resp);
  }

  // 5. Second client heartbeats
  {
    HeartbeatRequest req;
    req.set_client_id("client-2");
    req.add_resource_ids(1);
    HeartbeatResponse resp;
    service_->Heartbeat(nullptr, &req, &resp);

    // Now shared between 2 clients
    EXPECT_DOUBLE_EQ(resp.allocations().at(1), 50.0);
  }

  // 6. First client queries allocation
  {
    GetAllocationRequest req;
    req.set_client_id("client-1");
    req.set_resource_id(1);
    GetAllocationResponse resp;
    service_->GetAllocation(nullptr, &req, &resp);

    EXPECT_DOUBLE_EQ(resp.allocated_rate(), 50.0);
  }

  // 7. First client leaves
  {
    UnregisterClientRequest req;
    req.set_client_id("client-1");
    UnregisterClientResponse resp;
    service_->UnregisterClient(nullptr, &req, &resp);
  }

  // 8. Second client now gets full allocation
  {
    GetAllocationRequest req;
    req.set_client_id("client-2");
    req.set_resource_id(1);
    GetAllocationResponse resp;
    service_->GetAllocation(nullptr, &req, &resp);

    EXPECT_DOUBLE_EQ(resp.allocated_rate(), 100.0);
  }
}

}  // namespace
}  // namespace throttling
