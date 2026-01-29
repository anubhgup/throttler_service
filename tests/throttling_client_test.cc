// Unit tests for ThrottlingClient
//
// Tests the client library using FakeThrottlingStub for full coverage
// without requiring a real gRPC server.

#include "throttling_client.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace throttling {
namespace {

// ============================================================================
// Test Fixture with Fake Stub
// ============================================================================

class ThrottlingClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_stub_ = new FakeThrottlingStub();
  }

  // Helper to create client with fake stub
  std::unique_ptr<ThrottlingClient> CreateClient(
      std::chrono::milliseconds heartbeat_interval = std::chrono::seconds(10)) {
    return std::make_unique<ThrottlingClient>(
        std::unique_ptr<ThrottlingStubInterface>(fake_stub_),
        "test-client",
        heartbeat_interval);
  }

  // Non-owning pointer - ownership transfers to client
  FakeThrottlingStub* fake_stub_ = nullptr;
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(ThrottlingClientTest, ConstructWithServerAddress) {
  ThrottlingClient client("localhost:50051", "test-client");
  EXPECT_FALSE(client.IsRunning());
  EXPECT_EQ(0, client.GetPendingCount());
}

TEST_F(ThrottlingClientTest, ConstructWithFakeStub) {
  auto client = CreateClient();
  EXPECT_FALSE(client->IsRunning());
  EXPECT_EQ(0, client->GetPendingCount());
}

TEST_F(ThrottlingClientTest, ConstructWithCustomHeartbeatInterval) {
  auto client = CreateClient(std::chrono::seconds(30));
  EXPECT_FALSE(client->IsRunning());
}

// ============================================================================
// Start/Stop Tests
// ============================================================================

TEST_F(ThrottlingClientTest, StartSucceedsWithFakeStub) {
  auto client = CreateClient();
  
  EXPECT_TRUE(client->Start());
  EXPECT_TRUE(client->IsRunning());
  EXPECT_EQ(1, fake_stub_->GetRegisterCallCount());
  EXPECT_EQ("test-client", fake_stub_->GetLastClientId());
  
  client->Stop();
  EXPECT_FALSE(client->IsRunning());
}

TEST_F(ThrottlingClientTest, StartFailsWhenRegisterFails) {
  fake_stub_->SetRegisterResult(
      grpc::Status(grpc::StatusCode::UNAVAILABLE, "Server down"));
  
  auto client = CreateClient();
  
  EXPECT_FALSE(client->Start());
  EXPECT_FALSE(client->IsRunning());
  EXPECT_EQ(1, fake_stub_->GetRegisterCallCount());
}

TEST_F(ThrottlingClientTest, StartIdempotent) {
  auto client = CreateClient();
  
  EXPECT_TRUE(client->Start());
  EXPECT_TRUE(client->Start());  // Second call is no-op
  EXPECT_EQ(1, fake_stub_->GetRegisterCallCount());
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, StopUnregisters) {
  auto client = CreateClient();
  
  client->Start();
  client->Stop();
  
  EXPECT_EQ(1, fake_stub_->GetUnregisterCallCount());
  EXPECT_FALSE(client->IsRunning());
}

TEST_F(ThrottlingClientTest, StopIdempotent) {
  auto client = CreateClient();
  
  client->Start();
  client->Stop();
  client->Stop();  // Second call is no-op
  
  EXPECT_EQ(1, fake_stub_->GetUnregisterCallCount());
}

TEST_F(ThrottlingClientTest, StopWhenNotRunningIsNoOp) {
  auto client = CreateClient();
  
  client->Stop();  // Never started
  EXPECT_EQ(0, fake_stub_->GetUnregisterCallCount());
}

TEST_F(ThrottlingClientTest, DestructorStopsClient) {
  {
    auto client = CreateClient();
    client->Start();
  }  // Destructor should call Stop
  
  EXPECT_EQ(1, fake_stub_->GetUnregisterCallCount());
}

// ============================================================================
// Resource Interest Tests
// ============================================================================

TEST_F(ThrottlingClientTest, SetResourceInterestsBeforeStart) {
  auto client = CreateClient();
  client->SetResourceInterests({1, 2, 3});
  
  EXPECT_TRUE(client->Start());
  
  // Initial heartbeat should include interests
  EXPECT_GE(fake_stub_->GetHeartbeatCallCount(), 1);
  auto last_ids = fake_stub_->GetLastResourceIds();
  EXPECT_EQ(3, last_ids.size());
  EXPECT_TRUE(last_ids.count(1));
  EXPECT_TRUE(last_ids.count(2));
  EXPECT_TRUE(last_ids.count(3));
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, SetEmptyResourceInterests) {
  auto client = CreateClient();
  client->SetResourceInterests({});
  
  EXPECT_TRUE(client->Start());
  EXPECT_TRUE(fake_stub_->GetLastResourceIds().empty());
  
  client->Stop();
}

// ============================================================================
// Allocation Tests
// ============================================================================

TEST_F(ThrottlingClientTest, GetAllocationAfterHeartbeat) {
  fake_stub_->SetAllocations({{1, 100.0}, {2, 50.0}});
  
  auto client = CreateClient();
  client->SetResourceInterests({1, 2});
  client->Start();
  
  // After start, initial heartbeat sets allocations
  EXPECT_EQ(100.0, client->GetAllocation(1));
  EXPECT_EQ(50.0, client->GetAllocation(2));
  EXPECT_EQ(0.0, client->GetAllocation(999));  // Not allocated
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, AllocationUpdatesOnHeartbeat) {
  fake_stub_->SetAllocations({{1, 100.0}});
  
  auto client = CreateClient(std::chrono::milliseconds(50));
  client->SetResourceInterests({1});
  client->Start();
  
  EXPECT_EQ(100.0, client->GetAllocation(1));
  
  // Change allocation
  fake_stub_->SetAllocations({{1, 200.0}});
  
  // Wait for heartbeat
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  EXPECT_EQ(200.0, client->GetAllocation(1));
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, AllocationRemovedWhenNotInHeartbeat) {
  fake_stub_->SetAllocations({{1, 100.0}, {2, 50.0}});
  
  auto client = CreateClient(std::chrono::milliseconds(50));
  client->SetResourceInterests({1, 2});
  client->Start();
  
  EXPECT_EQ(100.0, client->GetAllocation(1));
  EXPECT_EQ(50.0, client->GetAllocation(2));
  
  // Remove resource 2 from allocations
  fake_stub_->SetAllocations({{1, 100.0}});
  
  // Wait for heartbeat
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  EXPECT_EQ(100.0, client->GetAllocation(1));
  EXPECT_EQ(0.0, client->GetAllocation(2));  // Removed
  
  client->Stop();
}

// ============================================================================
// Acquire Tests
// ============================================================================

TEST_F(ThrottlingClientTest, AcquireExecutesImmediatelyWithTokens) {
  fake_stub_->SetAllocations({{1, 100.0}});  // 100 tokens/sec, burst=100
  
  auto client = CreateClient();
  client->SetResourceInterests({1});
  client->Start();
  
  std::atomic<int> callback_count{0};
  
  // Should execute immediately (have 100 tokens)
  client->Acquire(1, 1.0, [&callback_count]() {
    callback_count++;
  });
  
  EXPECT_EQ(1, callback_count.load());
  EXPECT_EQ(0, client->GetPendingCount());
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, AcquireQueuesWhenNoTokens) {
  fake_stub_->SetAllocations({{1, 10.0}});  // 10 tokens/sec, burst=10
  
  auto client = CreateClient();
  client->SetResourceInterests({1});
  client->Start();
  
  std::atomic<int> callback_count{0};
  
  // Consume all tokens
  for (int i = 0; i < 10; i++) {
    client->Acquire(1, 1.0, [&callback_count]() {
      callback_count++;
    });
  }
  
  EXPECT_EQ(10, callback_count.load());
  
  // Next acquire should queue
  client->Acquire(1, 1.0, [&callback_count]() {
    callback_count++;
  });
  
  EXPECT_EQ(10, callback_count.load());  // Not executed yet
  EXPECT_EQ(1, client->GetPendingCount());
  
  // Wait for tokens to refill
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  
  EXPECT_EQ(11, callback_count.load());  // Now executed
  EXPECT_EQ(0, client->GetPendingCount());
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, AcquireQueuesWhenNoAllocation) {
  auto client = CreateClient();
  // No allocations set - bucket doesn't exist
  client->Start();
  
  std::atomic<int> callback_count{0};
  
  client->Acquire(1, 1.0, [&callback_count]() {
    callback_count++;
  });
  
  EXPECT_EQ(0, callback_count.load());
  EXPECT_EQ(1, client->GetPendingCount());
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, AcquireMultipleTokens) {
  fake_stub_->SetAllocations({{1, 100.0}});  // burst=100
  
  auto client = CreateClient();
  client->SetResourceInterests({1});
  client->Start();
  
  std::atomic<int> callback_count{0};
  
  // Acquire 50 tokens
  client->Acquire(1, 50.0, [&callback_count]() {
    callback_count++;
  });
  
  EXPECT_EQ(1, callback_count.load());
  
  // Acquire another 50 tokens
  client->Acquire(1, 50.0, [&callback_count]() {
    callback_count++;
  });
  
  EXPECT_EQ(2, callback_count.load());
  
  // Acquire 1 more - should queue
  client->Acquire(1, 1.0, [&callback_count]() {
    callback_count++;
  });
  
  EXPECT_EQ(2, callback_count.load());
  EXPECT_EQ(1, client->GetPendingCount());
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, AcquireWithNullCallback) {
  fake_stub_->SetAllocations({{1, 100.0}});
  
  auto client = CreateClient();
  client->SetResourceInterests({1});
  client->Start();
  
  // Should not crash
  client->Acquire(1, 1.0, nullptr);
  EXPECT_EQ(0, client->GetPendingCount());
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, AcquireQueuedCallbackExecutedInOrder) {
  fake_stub_->SetAllocations({{1, 10.0}});  // 10 tokens/sec
  
  auto client = CreateClient();
  client->SetResourceInterests({1});
  client->Start();
  
  std::vector<int> execution_order;
  std::mutex order_mutex;
  
  // Consume all tokens
  for (int i = 0; i < 10; i++) {
    client->Acquire(1, 1.0, [](){});
  }
  
  // Queue 3 more
  for (int i = 0; i < 3; i++) {
    int idx = i;
    client->Acquire(1, 1.0, [&execution_order, &order_mutex, idx]() {
      std::lock_guard<std::mutex> lock(order_mutex);
      execution_order.push_back(idx);
    });
  }
  
  EXPECT_EQ(3, client->GetPendingCount());
  
  // Wait for all to execute
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  
  // Check order
  std::lock_guard<std::mutex> lock(order_mutex);
  ASSERT_EQ(3, execution_order.size());
  EXPECT_EQ(0, execution_order[0]);
  EXPECT_EQ(1, execution_order[1]);
  EXPECT_EQ(2, execution_order[2]);
  
  client->Stop();
}

// ============================================================================
// Heartbeat Failure Tests
// ============================================================================

TEST_F(ThrottlingClientTest, HeartbeatFailureDoesNotCrash) {
  fake_stub_->SetHeartbeatResult(
      grpc::Status(grpc::StatusCode::UNAVAILABLE, "Server down"));
  
  auto client = CreateClient(std::chrono::milliseconds(50));
  client->SetResourceInterests({1});
  client->Start();
  
  // Wait for a few heartbeats
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  
  // Client should still be running
  EXPECT_TRUE(client->IsRunning());
  
  client->Stop();
}

// ============================================================================
// Concurrent Access Tests
// ============================================================================

TEST_F(ThrottlingClientTest, ConcurrentAcquire) {
  fake_stub_->SetAllocations({{1, 1000.0}});  // High rate
  
  auto client = CreateClient();
  client->SetResourceInterests({1});
  client->Start();
  
  std::atomic<int> callback_count{0};
  std::vector<std::thread> threads;
  
  for (int i = 0; i < 10; i++) {
    threads.emplace_back([&client, &callback_count]() {
      for (int j = 0; j < 100; j++) {
        client->Acquire(1, 1.0, [&callback_count]() {
          callback_count++;
        });
      }
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  
  // Wait for any queued callbacks
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  
  EXPECT_EQ(1000, callback_count.load());
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, ConcurrentSetResourceInterests) {
  auto client = CreateClient();
  client->Start();
  
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; i++) {
    threads.emplace_back([&client, i]() {
      for (int j = 0; j < 100; j++) {
        client->SetResourceInterests({static_cast<int64_t>(i),
                                      static_cast<int64_t>(j)});
      }
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  
  client->Stop();
  SUCCEED();  // No crash = success
}

TEST_F(ThrottlingClientTest, ConcurrentGetAllocation) {
  fake_stub_->SetAllocations({{1, 100.0}});
  
  auto client = CreateClient();
  client->SetResourceInterests({1});
  client->Start();
  
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; i++) {
    threads.emplace_back([&client]() {
      for (int j = 0; j < 100; j++) {
        client->GetAllocation(1);
        client->GetAllocation(static_cast<int64_t>(j));
      }
    });
  }
  
  for (auto& t : threads) {
    t.join();
  }
  
  client->Stop();
  SUCCEED();  // No crash = success
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(ThrottlingClientTest, GetAllocationNegativeResourceId) {
  auto client = CreateClient();
  EXPECT_EQ(0.0, client->GetAllocation(-1));
}

TEST_F(ThrottlingClientTest, AcquireZeroTokens) {
  fake_stub_->SetAllocations({{1, 100.0}});
  
  auto client = CreateClient();
  client->SetResourceInterests({1});
  client->Start();
  
  std::atomic<bool> called{false};
  client->Acquire(1, 0.0, [&called]() {
    called = true;
  });
  
  EXPECT_TRUE(called.load());  // 0 tokens always succeeds
  
  client->Stop();
}

TEST_F(ThrottlingClientTest, StopDiscardsQueuedRequests) {
  fake_stub_->SetAllocations({{1, 1.0}});  // Very slow rate
  
  auto client = CreateClient();
  client->SetResourceInterests({1});
  client->Start();
  
  std::atomic<int> callback_count{0};
  
  // Consume token and queue many requests
  client->Acquire(1, 1.0, [&callback_count]() { callback_count++; });
  for (int i = 0; i < 100; i++) {
    client->Acquire(1, 1.0, [&callback_count]() { callback_count++; });
  }
  
  EXPECT_EQ(1, callback_count.load());
  // Worker thread may have started processing, so count could be 99-100
  EXPECT_GE(client->GetPendingCount(), 99);
  
  int count_before_stop = callback_count.load();
  client->Stop();
  
  // Most queued requests should be discarded (only a few may have executed)
  EXPECT_LT(callback_count.load(), 10);
  EXPECT_EQ(0, client->GetPendingCount());
}

// ============================================================================
// FakeThrottlingStub Tests
// ============================================================================

TEST(FakeThrottlingStubTest, DefaultsToOkStatus) {
  FakeThrottlingStub stub;
  
  RegisterClientRequest reg_req;
  RegisterClientResponse reg_resp;
  EXPECT_TRUE(stub.RegisterClient(reg_req, &reg_resp).ok());
  
  UnregisterClientRequest unreg_req;
  UnregisterClientResponse unreg_resp;
  EXPECT_TRUE(stub.UnregisterClient(unreg_req, &unreg_resp).ok());
  
  HeartbeatRequest hb_req;
  HeartbeatResponse hb_resp;
  EXPECT_TRUE(stub.Heartbeat(hb_req, &hb_resp).ok());
}

TEST(FakeThrottlingStubTest, CountsCalls) {
  FakeThrottlingStub stub;
  
  EXPECT_EQ(0, stub.GetRegisterCallCount());
  EXPECT_EQ(0, stub.GetUnregisterCallCount());
  EXPECT_EQ(0, stub.GetHeartbeatCallCount());
  
  RegisterClientRequest reg_req;
  RegisterClientResponse reg_resp;
  stub.RegisterClient(reg_req, &reg_resp);
  stub.RegisterClient(reg_req, &reg_resp);
  
  EXPECT_EQ(2, stub.GetRegisterCallCount());
  
  UnregisterClientRequest unreg_req;
  UnregisterClientResponse unreg_resp;
  stub.UnregisterClient(unreg_req, &unreg_resp);
  
  EXPECT_EQ(1, stub.GetUnregisterCallCount());
  
  HeartbeatRequest hb_req;
  HeartbeatResponse hb_resp;
  stub.Heartbeat(hb_req, &hb_resp);
  stub.Heartbeat(hb_req, &hb_resp);
  stub.Heartbeat(hb_req, &hb_resp);
  
  EXPECT_EQ(3, stub.GetHeartbeatCallCount());
}

TEST(FakeThrottlingStubTest, ReturnsConfiguredAllocations) {
  FakeThrottlingStub stub;
  stub.SetAllocations({{1, 100.0}, {2, 50.0}});
  
  HeartbeatRequest request;
  HeartbeatResponse response;
  stub.Heartbeat(request, &response);
  
  EXPECT_EQ(2, response.allocations_size());
  EXPECT_EQ(100.0, response.allocations().at(1));
  EXPECT_EQ(50.0, response.allocations().at(2));
}

TEST(FakeThrottlingStubTest, TracksLastClientId) {
  FakeThrottlingStub stub;
  
  RegisterClientRequest req;
  req.set_client_id("client-123");
  RegisterClientResponse resp;
  stub.RegisterClient(req, &resp);
  
  EXPECT_EQ("client-123", stub.GetLastClientId());
}

TEST(FakeThrottlingStubTest, TracksLastResourceIds) {
  FakeThrottlingStub stub;
  
  HeartbeatRequest req;
  req.set_client_id("client-1");
  req.add_resource_ids(10);
  req.add_resource_ids(20);
  req.add_resource_ids(30);
  
  HeartbeatResponse resp;
  stub.Heartbeat(req, &resp);
  
  auto ids = stub.GetLastResourceIds();
  EXPECT_EQ(3, ids.size());
  EXPECT_TRUE(ids.count(10));
  EXPECT_TRUE(ids.count(20));
  EXPECT_TRUE(ids.count(30));
}

TEST(FakeThrottlingStubTest, SetUnregisterResult) {
  FakeThrottlingStub stub;
  stub.SetUnregisterResult(
      grpc::Status(grpc::StatusCode::INTERNAL, "Error"));
  UnregisterClientRequest req;
  UnregisterClientResponse resp;
  auto status = stub.UnregisterClient(req, &resp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(grpc::StatusCode::INTERNAL, status.error_code());
}

TEST(FakeThrottlingStubTest, SetResourceLimitTracksCall) {
  FakeThrottlingStub stub;
  EXPECT_EQ(0, stub.GetSetResourceLimitCallCount());
  SetResourceLimitRequest req;
  req.set_resource_id(42);
  req.set_rate_limit(100.0);
  SetResourceLimitResponse resp;
  auto status = stub.SetResourceLimit(req, &resp);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(1, stub.GetSetResourceLimitCallCount());
  EXPECT_EQ(42, stub.GetLastSetResourceId());
  EXPECT_EQ(100.0, stub.GetLastSetRateLimit());
}

TEST(FakeThrottlingStubTest, SetSetResourceLimitResult) {
  FakeThrottlingStub stub;
  stub.SetSetResourceLimitResult(
      grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Bad rate"));
  SetResourceLimitRequest req;
  SetResourceLimitResponse resp;
  auto status = stub.SetResourceLimit(req, &resp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(grpc::StatusCode::INVALID_ARGUMENT, status.error_code());
}

// ============================================================================
// ThrottlingClient SetResourceLimit Tests
// ============================================================================

class SetResourceLimitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_stub_ = new FakeThrottlingStub();
  }

  std::unique_ptr<ThrottlingClient> CreateClient() {
    return std::make_unique<ThrottlingClient>(
        std::unique_ptr<ThrottlingStubInterface>(fake_stub_),
        "test-client",
        std::chrono::seconds(10));
  }

  FakeThrottlingStub* fake_stub_ = nullptr;
};

TEST_F(SetResourceLimitTest, SetResourceLimitSuccess) {
  auto client = CreateClient();
  bool result = client->SetResourceLimit(1, 100.0);
  EXPECT_TRUE(result);
  EXPECT_EQ(1, fake_stub_->GetSetResourceLimitCallCount());
  EXPECT_EQ(1, fake_stub_->GetLastSetResourceId());
  EXPECT_EQ(100.0, fake_stub_->GetLastSetRateLimit());
}

TEST_F(SetResourceLimitTest, SetResourceLimitFailure) {
  fake_stub_->SetSetResourceLimitResult(
      grpc::Status(grpc::StatusCode::UNAVAILABLE, "Server down"));
  auto client = CreateClient();
  bool result = client->SetResourceLimit(1, 100.0);
  EXPECT_FALSE(result);
  EXPECT_EQ(1, fake_stub_->GetSetResourceLimitCallCount());
}

TEST_F(SetResourceLimitTest, SetResourceLimitWithZeroRate) {
  auto client = CreateClient();
  bool result = client->SetResourceLimit(1, 0.0);
  EXPECT_TRUE(result);
  EXPECT_EQ(0.0, fake_stub_->GetLastSetRateLimit());
}

TEST_F(SetResourceLimitTest, SetResourceLimitMultipleCalls) {
  auto client = CreateClient();
  client->SetResourceLimit(1, 100.0);
  client->SetResourceLimit(2, 200.0);
  client->SetResourceLimit(1, 150.0);
  EXPECT_EQ(3, fake_stub_->GetSetResourceLimitCallCount());
  EXPECT_EQ(1, fake_stub_->GetLastSetResourceId());
  EXPECT_EQ(150.0, fake_stub_->GetLastSetRateLimit());
}

TEST_F(SetResourceLimitTest, SetResourceLimitBeforeStart) {
  auto client = CreateClient();
  // Client not started - should still work (admin operation)
  bool result = client->SetResourceLimit(1, 100.0);
  EXPECT_TRUE(result);
}

}  // namespace
}  // namespace throttling

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
