// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/extras/sqlite/sqlite_persistent_reporting_and_nel_store.h"

#include <memory>
#include <vector>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/post_task.h"
#include "base/test/bind_test_util.h"
#include "base/test/simple_test_clock.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/network_error_logging/network_error_logging_service.h"
#include "net/reporting/reporting_test_util.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

const base::FilePath::CharType kReportingAndNELStoreFilename[] =
    FILE_PATH_LITERAL("ReportingAndNEL");

const GURL kUrl1 = GURL("https://www.foo.test");
const GURL kUrl2 = GURL("https://www.bar.test");
const url::Origin kOrigin1 = url::Origin::Create(kUrl1);
const url::Origin kOrigin2 = url::Origin::Create(kUrl2);
const IPAddress kServerIP = IPAddress(192, 168, 0, 1);
const std::string kHeader = "{\"report_to\":\"group\",\"max_age\":86400}";
const std::string kHeaderMaxAge0 = "{\"report_to\":\"group\",\"max_age\":0}";
const std::string kGroupName1 = "group1";
const std::string kGroupName2 = "group2";
const GURL kEndpoint1 = GURL("https://endpoint.test/1");
const GURL kEndpoint2 = GURL("https://endpoint.test/2");
const base::Time kExpires = base::Time::Now() + base::TimeDelta::FromDays(7);

enum class Op { kAdd, kDelete, kUpdate, kUpdateDetails };

struct TestCase {
  std::vector<Op> operations;
  size_t expected_queue_length;
};

// Testcases for coalescing of pending operations. In each case, the given
// sequence of operations should be coalesced down to |expected_queue_length|
// actual operations queued.
const std::vector<TestCase> kCoalescingTestcases = {
    {{Op::kAdd, Op::kDelete}, 1u},
    {{Op::kUpdate, Op::kDelete}, 1u},
    {{Op::kAdd, Op::kUpdate, Op::kDelete}, 1u},
    {{Op::kUpdate, Op::kUpdate}, 1u},
    {{Op::kAdd, Op::kUpdate, Op::kUpdate}, 2u},
    {{Op::kDelete, Op::kAdd}, 2u},
    {{Op::kDelete, Op::kAdd, Op::kUpdate}, 3u},
    {{Op::kDelete, Op::kAdd, Op::kUpdate, Op::kUpdate}, 3u},
    {{Op::kDelete, Op::kDelete}, 1u},
    {{Op::kDelete, Op::kAdd, Op::kDelete}, 1u},
    {{Op::kDelete, Op::kAdd, Op::kUpdate, Op::kDelete}, 1u}};

// This is for Reporting endpoint groups, which have both UPDATE_DETAILS and
// UPDATE_ACCESS_TIME. These additional testcases test that UPDATE_DETAILS
// overwrites UPDATE_ACCESS_TIME, but not vice versa.
const std::vector<TestCase> kCoalescingTestcasesForUpdateDetails = {
    {{Op::kUpdateDetails, Op::kDelete}, 1u},
    {{Op::kAdd, Op::kUpdateDetails, Op::kDelete}, 1u},
    {{Op::kUpdateDetails, Op::kUpdateDetails}, 1u},
    {{Op::kUpdate, Op::kUpdateDetails}, 1u},
    {{Op::kUpdateDetails, Op::kUpdate}, 2u},
    {{Op::kAdd, Op::kUpdateDetails, Op::kUpdate}, 3u},
    {{Op::kAdd, Op::kUpdateDetails, Op::kUpdate, Op::kUpdateDetails}, 2u},
    {{Op::kDelete, Op::kAdd, Op::kUpdateDetails}, 3u},
    {{Op::kDelete, Op::kAdd, Op::kUpdateDetails, Op::kUpdateDetails}, 3u},
    {{Op::kDelete, Op::kAdd, Op::kUpdate, Op::kUpdateDetails}, 3u},
    {{Op::kDelete, Op::kAdd, Op::kUpdateDetails, Op::kUpdate}, 4u}};

}  // namespace

class SQLitePersistentReportingAndNELStoreTest
    : public TestWithScopedTaskEnvironment {
 public:
  SQLitePersistentReportingAndNELStoreTest() {}

  void CreateStore() {
    store_ = std::make_unique<SQLitePersistentReportingAndNELStore>(
        temp_dir_.GetPath().Append(kReportingAndNELStoreFilename),
        client_task_runner_, background_task_runner_);
  }

  void DestroyStore() {
    store_.reset();
    // Make sure we wait until the destructor has run by running all
    // ScopedTaskEnvironment tasks.
    RunUntilIdle();
  }

  // Call this on a brand new database that should have nothing stored in it.
  void InitializeStore() {
    std::vector<NetworkErrorLoggingService::NELPolicy> nel_policies;
    LoadNELPolicies(&nel_policies);
    EXPECT_EQ(0u, nel_policies.size());

    // One load should be sufficient to initialize the database, but we might as
    // well load everything to check that there is nothing in the database.
    std::vector<ReportingClient> endpoints;
    std::vector<CachedReportingEndpointGroup> groups;
    LoadReportingClients(&endpoints, &groups);
    EXPECT_EQ(0u, endpoints.size());
    EXPECT_EQ(0u, groups.size());
  }

  void LoadNELPolicies(
      std::vector<NetworkErrorLoggingService::NELPolicy>* policies_out) {
    base::RunLoop run_loop;
    store_->LoadNELPolicies(base::BindRepeating(
        &SQLitePersistentReportingAndNELStoreTest::OnNELPoliciesLoaded,
        base::Unretained(this), &run_loop, policies_out));
    run_loop.Run();
  }

  void OnNELPoliciesLoaded(
      base::RunLoop* run_loop,
      std::vector<NetworkErrorLoggingService::NELPolicy>* policies_out,
      std::vector<NetworkErrorLoggingService::NELPolicy> policies) {
    policies_out->swap(policies);
    run_loop->Quit();
  }

  void LoadReportingClients(
      std::vector<ReportingClient>* endpoints_out,
      std::vector<CachedReportingEndpointGroup>* groups_out) {
    base::RunLoop run_loop;
    store_->LoadReportingClients(base::BindRepeating(
        &SQLitePersistentReportingAndNELStoreTest::OnReportingClientsLoaded,
        base::Unretained(this), &run_loop, endpoints_out, groups_out));
    run_loop.Run();
  }

  void OnReportingClientsLoaded(
      base::RunLoop* run_loop,
      std::vector<ReportingClient>* endpoints_out,
      std::vector<CachedReportingEndpointGroup>* groups_out,
      std::vector<ReportingClient> endpoints,
      std::vector<CachedReportingEndpointGroup> groups) {
    endpoints_out->swap(endpoints);
    groups_out->swap(groups);
    run_loop->Quit();
  }

  std::string ReadRawDBContents() {
    std::string contents;
    if (!base::ReadFileToString(
            temp_dir_.GetPath().Append(kReportingAndNELStoreFilename),
            &contents)) {
      return std::string();
    }
    return contents;
  }

  bool WithinOneMicrosecond(base::Time t1, base::Time t2) {
    base::TimeDelta delta = t1 - t2;
    return delta.magnitude() < base::TimeDelta::FromMicroseconds(1);
  }

  void WaitOnEvent(base::WaitableEvent* event) {
    base::ScopedAllowBaseSyncPrimitivesForTesting allow_base_sync_primitives;
    event->Wait();
  }

  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  void TearDown() override { DestroyStore(); }

  NetworkErrorLoggingService::NELPolicy MakeNELPolicy(url::Origin origin,
                                                      base::Time last_used) {
    NetworkErrorLoggingService::NELPolicy policy;
    policy.origin = origin;
    policy.received_ip_address = IPAddress::IPv4Localhost();
    policy.report_to = "group";
    policy.expires = kExpires;
    policy.success_fraction = 0.0;
    policy.failure_fraction = 1.0;
    policy.include_subdomains = false;
    policy.last_used = last_used;
    return policy;
  }

  ReportingClient MakeReportingEndpoint(
      url::Origin origin,
      std::string group_name,
      GURL url,
      int priority = ReportingClient::EndpointInfo::kDefaultPriority,
      int weight = ReportingClient::EndpointInfo::kDefaultWeight) {
    ReportingClient::EndpointInfo info;
    info.url = url;
    info.priority = priority;
    info.weight = weight;
    ReportingClient endpoint(origin, group_name, std::move(info));
    return endpoint;
  }

  CachedReportingEndpointGroup MakeReportingEndpointGroup(
      url::Origin origin,
      std::string group_name,
      base::Time last_used,
      OriginSubdomains include_subdomains = OriginSubdomains::DEFAULT,
      base::Time expires = kExpires) {
    return CachedReportingEndpointGroup(origin, group_name, include_subdomains,
                                        expires, last_used);
  }

 protected:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<SQLitePersistentReportingAndNELStore> store_;
  const scoped_refptr<base::SequencedTaskRunner> client_task_runner_ =
      base::ThreadTaskRunnerHandle::Get();
  const scoped_refptr<base::SequencedTaskRunner> background_task_runner_ =
      base::CreateSequencedTaskRunnerWithTraits({base::MayBlock()});
};

TEST_F(SQLitePersistentReportingAndNELStoreTest, CreateDBAndTables) {
  CreateStore();
  InitializeStore();
  EXPECT_NE(nullptr, store_.get());
  std::string contents = ReadRawDBContents();
  EXPECT_NE("", contents);
  EXPECT_NE(std::string::npos, contents.find("nel_policies"));
  EXPECT_NE(std::string::npos, contents.find("reporting_endpoints"));
  EXPECT_NE(std::string::npos, contents.find("reporting_endpoint_groups"));
}

TEST_F(SQLitePersistentReportingAndNELStoreTest, PersistNELPolicy) {
  CreateStore();
  InitializeStore();
  base::Time now = base::Time::Now();
  NetworkErrorLoggingService::NELPolicy policy = MakeNELPolicy(kOrigin1, now);
  store_->AddNELPolicy(policy);

  // Close and reopen the database.
  DestroyStore();
  CreateStore();

  // Load the stored policy.
  std::vector<NetworkErrorLoggingService::NELPolicy> policies;
  LoadNELPolicies(&policies);
  ASSERT_EQ(1u, policies.size());
  EXPECT_EQ(policy.origin, policies[0].origin);
  EXPECT_EQ(policy.received_ip_address, policies[0].received_ip_address);
  EXPECT_EQ(policy.report_to, policies[0].report_to);
  EXPECT_TRUE(WithinOneMicrosecond(policy.expires, policies[0].expires));
  EXPECT_EQ(policy.include_subdomains, policies[0].include_subdomains);
  EXPECT_EQ(policy.success_fraction, policies[0].success_fraction);
  EXPECT_EQ(policy.failure_fraction, policies[0].failure_fraction);
  EXPECT_TRUE(WithinOneMicrosecond(policy.last_used, policies[0].last_used));
}

TEST_F(SQLitePersistentReportingAndNELStoreTest, LoadFailed) {
  // Inject a db initialization failure by creating a directory where the db
  // file should be.
  ASSERT_TRUE(base::CreateDirectory(
      temp_dir_.GetPath().Append(kReportingAndNELStoreFilename)));
  store_ = std::make_unique<SQLitePersistentReportingAndNELStore>(
      temp_dir_.GetPath().Append(kReportingAndNELStoreFilename),
      client_task_runner_, background_task_runner_);

  // InitializeStore() checks that we receive empty vectors of NEL policies,
  // reporting endpoints, and reporting endpoint groups, signifying the failure
  // to load.
  InitializeStore();
}

TEST_F(SQLitePersistentReportingAndNELStoreTest, UpdateNELPolicyAccessTime) {
  CreateStore();
  InitializeStore();
  base::Time now = base::Time::Now();
  NetworkErrorLoggingService::NELPolicy policy = MakeNELPolicy(kOrigin1, now);
  store_->AddNELPolicy(policy);

  policy.last_used = now + base::TimeDelta::FromDays(1);
  store_->UpdateNELPolicyAccessTime(policy);

  // Close and reopen the database.
  DestroyStore();
  CreateStore();

  // Load the stored policy.
  std::vector<NetworkErrorLoggingService::NELPolicy> policies;
  LoadNELPolicies(&policies);
  ASSERT_EQ(1u, policies.size());
  EXPECT_EQ(policy.origin, policies[0].origin);
  EXPECT_TRUE(WithinOneMicrosecond(policy.last_used, policies[0].last_used));
}

TEST_F(SQLitePersistentReportingAndNELStoreTest, DeleteNELPolicy) {
  CreateStore();
  InitializeStore();
  base::Time now = base::Time::Now();
  NetworkErrorLoggingService::NELPolicy policy1 = MakeNELPolicy(kOrigin1, now);
  NetworkErrorLoggingService::NELPolicy policy2 = MakeNELPolicy(kOrigin2, now);
  store_->AddNELPolicy(policy1);
  store_->AddNELPolicy(policy2);

  store_->DeleteNELPolicy(policy1);

  // Close and reopen the database.
  DestroyStore();
  CreateStore();

  // |policy1| is no longer in the database but |policy2| remains.
  std::vector<NetworkErrorLoggingService::NELPolicy> policies;
  LoadNELPolicies(&policies);
  ASSERT_EQ(1u, policies.size());
  EXPECT_EQ(policy2.origin, policies[0].origin);

  // Delete after having closed and reopened.
  store_->DeleteNELPolicy(policy2);
  DestroyStore();
  CreateStore();

  // |policy2| is also gone.
  policies.clear();
  LoadNELPolicies(&policies);
  EXPECT_EQ(0u, policies.size());
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       NELPolicyUniquenessConstraint) {
  CreateStore();
  InitializeStore();
  base::Time now = base::Time::Now();
  NetworkErrorLoggingService::NELPolicy policy1 = MakeNELPolicy(kOrigin1, now);
  // Different NEL policy (different last_used) with the same origin.
  NetworkErrorLoggingService::NELPolicy policy2 =
      MakeNELPolicy(kOrigin1, now + base::TimeDelta::FromDays(1));

  store_->AddNELPolicy(policy1);
  // Adding a policy with the same origin should trigger a warning and fail to
  // execute.
  store_->AddNELPolicy(policy2);

  // Close and reopen the database.
  DestroyStore();
  CreateStore();

  std::vector<NetworkErrorLoggingService::NELPolicy> policies;
  LoadNELPolicies(&policies);
  // Only the first policy we added should be in the store.
  ASSERT_EQ(1u, policies.size());
  EXPECT_EQ(policy1.origin, policies[0].origin);
  EXPECT_TRUE(WithinOneMicrosecond(policy1.last_used, policies[0].last_used));
}

TEST_F(SQLitePersistentReportingAndNELStoreTest, CoalesceNELPolicyOperations) {
  NetworkErrorLoggingService::NELPolicy policy =
      MakeNELPolicy(kOrigin1, base::Time::Now());

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  for (const TestCase& testcase : kCoalescingTestcases) {
    CreateStore();
    base::RunLoop run_loop;
    store_->LoadNELPolicies(base::BindLambdaForTesting(
        [&](std::vector<NetworkErrorLoggingService::NELPolicy>) {
          run_loop.Quit();
        }));
    run_loop.Run();

    // Wedge the background thread to make sure it doesn't start consuming the
    // queue.
    background_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SQLitePersistentReportingAndNELStoreTest::WaitOnEvent,
                       base::Unretained(this), &event));

    // Now run the ops, and check how much gets queued.
    for (const Op op : testcase.operations) {
      switch (op) {
        case Op::kAdd:
          store_->AddNELPolicy(policy);
          break;

        case Op::kDelete:
          store_->DeleteNELPolicy(policy);
          break;

        case Op::kUpdate:
          store_->UpdateNELPolicyAccessTime(policy);
          break;

        default:
          NOTREACHED();
          break;
      }
    }

    EXPECT_EQ(testcase.expected_queue_length,
              store_->GetQueueLengthForTesting());

    event.Signal();
    RunUntilIdle();
  }
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       DontCoalesceUnrelatedNELPolicies) {
  CreateStore();
  InitializeStore();

  base::Time now = base::Time::Now();
  NetworkErrorLoggingService::NELPolicy policy1 = MakeNELPolicy(kOrigin1, now);
  NetworkErrorLoggingService::NELPolicy policy2 = MakeNELPolicy(kOrigin2, now);

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  // Wedge the background thread to make sure it doesn't start consuming the
  // queue.
  background_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SQLitePersistentReportingAndNELStoreTest::WaitOnEvent,
                     base::Unretained(this), &event));

  // Delete on |policy2| should not cancel addition of unrelated |policy1|.
  store_->AddNELPolicy(policy1);
  store_->DeleteNELPolicy(policy2);
  EXPECT_EQ(2u, store_->GetQueueLengthForTesting());

  event.Signal();
  RunUntilIdle();
}

// These tests test that a SQLitePersistentReportingAndNELStore
// can be used by a NetworkErrorLoggingService to persist NEL policies.
class SQLitePersistNELTest : public SQLitePersistentReportingAndNELStoreTest {
 public:
  SQLitePersistNELTest() {}

  void SetUp() override {
    SQLitePersistentReportingAndNELStoreTest::SetUp();
    SetUpNetworkErrorLoggingService();
  }

  void TearDown() override {
    service_->OnShutdown();
    service_.reset();
    reporting_service_.reset();
    SQLitePersistentReportingAndNELStoreTest::TearDown();
  }

  void SetUpNetworkErrorLoggingService() {
    CreateStore();
    service_ = NetworkErrorLoggingService::Create(store_.get());
    reporting_service_ = std::make_unique<TestReportingService>();
    service_->SetReportingService(reporting_service_.get());
    service_->SetClockForTesting(&clock_);
  }

  void SimulateRestart() {
    TearDown();
    SetUpNetworkErrorLoggingService();
  }

  NetworkErrorLoggingService::RequestDetails MakeRequestDetails(
      GURL url,
      Error error_type) {
    NetworkErrorLoggingService::RequestDetails details;

    details.uri = url;
    details.referrer = GURL("https://referrer.com/");
    details.user_agent = "Mozilla/1.0";
    details.server_ip = kServerIP;
    details.method = "GET";
    details.status_code = 0;
    details.elapsed_time = base::TimeDelta::FromSeconds(1);
    details.type = error_type;
    details.reporting_upload_depth = 0;

    return details;
  }

 protected:
  base::SimpleTestClock clock_;
  std::unique_ptr<NetworkErrorLoggingService> service_;
  std::unique_ptr<TestReportingService> reporting_service_;
};

TEST_F(SQLitePersistNELTest, AddAndRetrieveNELPolicy) {
  service_->OnHeader(kOrigin1, kServerIP, kHeader);
  RunUntilIdle();

  EXPECT_EQ(1u, service_->GetPolicyOriginsForTesting().count(kOrigin1));
  SimulateRestart();

  service_->OnRequest(MakeRequestDetails(kUrl1, ERR_INVALID_RESPONSE));
  RunUntilIdle();

  EXPECT_EQ(1u, service_->GetPolicyOriginsForTesting().count(kOrigin1));

  EXPECT_THAT(reporting_service_->reports(),
              testing::ElementsAre(ReportUrlIs(kUrl1)));
}

TEST_F(SQLitePersistNELTest, AddAndDeleteNELPolicy) {
  service_->OnHeader(kOrigin1, kServerIP, kHeader);
  RunUntilIdle();

  EXPECT_EQ(1u, service_->GetPolicyOriginsForTesting().count(kOrigin1));
  SimulateRestart();

  // Deletes the stored policy.
  service_->OnHeader(kOrigin1, kServerIP, kHeaderMaxAge0);
  RunUntilIdle();

  EXPECT_EQ(0u, service_->GetPolicyOriginsForTesting().count(kOrigin1));
  SimulateRestart();

  service_->OnRequest(MakeRequestDetails(kUrl1, ERR_INVALID_RESPONSE));
  RunUntilIdle();

  EXPECT_EQ(0u, service_->GetPolicyOriginsForTesting().count(kOrigin1));
  EXPECT_EQ(0u, reporting_service_->reports().size());
}

TEST_F(SQLitePersistNELTest, ExpirationTimeIsPersisted) {
  service_->OnHeader(kOrigin1, kServerIP, kHeader);
  RunUntilIdle();

  // Makes the policy we just added expired.
  clock_.Advance(base::TimeDelta::FromSeconds(86401));

  SimulateRestart();

  service_->OnRequest(MakeRequestDetails(kUrl1, ERR_INVALID_RESPONSE));
  RunUntilIdle();

  EXPECT_EQ(0u, reporting_service_->reports().size());

  // Add the policy again so that it is not expired.
  service_->OnHeader(kOrigin1, kServerIP, kHeader);

  SimulateRestart();

  service_->OnRequest(MakeRequestDetails(kUrl1, ERR_INVALID_RESPONSE));
  RunUntilIdle();

  EXPECT_THAT(reporting_service_->reports(),
              testing::ElementsAre(ReportUrlIs(kUrl1)));
}

TEST_F(SQLitePersistNELTest, OnRequestUpdatesAccessTime) {
  service_->OnHeader(kOrigin1, kServerIP, kHeader);
  RunUntilIdle();

  SimulateRestart();

  // Update the access time by sending a request.
  clock_.Advance(base::TimeDelta::FromSeconds(100));
  service_->OnRequest(MakeRequestDetails(kUrl1, ERR_INVALID_RESPONSE));
  RunUntilIdle();

  EXPECT_THAT(reporting_service_->reports(),
              testing::ElementsAre(ReportUrlIs(kUrl1)));

  SimulateRestart();
  // Check that the policy's access time has been updated.
  base::Time now = clock_.Now();
  NetworkErrorLoggingService::NELPolicy policy = MakeNELPolicy(kOrigin1, now);
  std::vector<NetworkErrorLoggingService::NELPolicy> policies;
  LoadNELPolicies(&policies);
  ASSERT_EQ(1u, policies.size());
  EXPECT_EQ(policy.origin, policies[0].origin);
  EXPECT_TRUE(WithinOneMicrosecond(policy.last_used, policies[0].last_used));
}

TEST_F(SQLitePersistNELTest, RemoveSomeBrowsingData) {
  service_->OnHeader(kOrigin1, kServerIP, kHeader);
  service_->OnHeader(kOrigin2, kServerIP, kHeader);
  RunUntilIdle();

  SimulateRestart();

  service_->OnRequest(MakeRequestDetails(kUrl1, ERR_INVALID_RESPONSE));
  RunUntilIdle();

  ASSERT_EQ(1u, service_->GetPolicyOriginsForTesting().count(kOrigin1));
  ASSERT_EQ(1u, service_->GetPolicyOriginsForTesting().count(kOrigin2));
  EXPECT_THAT(reporting_service_->reports(),
              testing::ElementsAre(ReportUrlIs(kUrl1)));

  SimulateRestart();

  service_->RemoveBrowsingData(
      base::BindRepeating([](const GURL& origin) -> bool {
        return origin.host() == kOrigin1.host();
      }));
  RunUntilIdle();

  EXPECT_EQ(0u, service_->GetPolicyOriginsForTesting().count(kOrigin1));
  EXPECT_EQ(1u, service_->GetPolicyOriginsForTesting().count(kOrigin2));

  SimulateRestart();

  service_->OnRequest(MakeRequestDetails(kUrl1, ERR_INVALID_RESPONSE));
  RunUntilIdle();
  EXPECT_EQ(0u, service_->GetPolicyOriginsForTesting().count(kOrigin1));
  EXPECT_EQ(1u, service_->GetPolicyOriginsForTesting().count(kOrigin2));
  EXPECT_EQ(0u, reporting_service_->reports().size());
}

TEST_F(SQLitePersistNELTest, RemoveAllBrowsingData) {
  service_->OnHeader(kOrigin1, kServerIP, kHeader);
  service_->OnHeader(kOrigin2, kServerIP, kHeader);
  RunUntilIdle();

  SimulateRestart();

  service_->OnRequest(MakeRequestDetails(kUrl1, ERR_INVALID_RESPONSE));
  service_->OnRequest(MakeRequestDetails(kUrl2, ERR_INVALID_RESPONSE));
  RunUntilIdle();

  ASSERT_EQ(1u, service_->GetPolicyOriginsForTesting().count(kOrigin1));
  ASSERT_EQ(1u, service_->GetPolicyOriginsForTesting().count(kOrigin2));
  EXPECT_THAT(reporting_service_->reports(),
              testing::ElementsAre(ReportUrlIs(kUrl1), ReportUrlIs(kUrl2)));

  SimulateRestart();

  service_->RemoveAllBrowsingData();
  RunUntilIdle();

  EXPECT_EQ(0u, service_->GetPolicyOriginsForTesting().count(kOrigin1));
  EXPECT_EQ(0u, service_->GetPolicyOriginsForTesting().count(kOrigin2));

  SimulateRestart();

  service_->OnRequest(MakeRequestDetails(kUrl1, ERR_INVALID_RESPONSE));
  service_->OnRequest(MakeRequestDetails(kUrl2, ERR_INVALID_RESPONSE));
  RunUntilIdle();
  EXPECT_EQ(0u, service_->GetPolicyOriginsForTesting().count(kOrigin1));
  EXPECT_EQ(0u, service_->GetPolicyOriginsForTesting().count(kOrigin2));
  EXPECT_EQ(0u, reporting_service_->reports().size());
}

TEST_F(SQLitePersistentReportingAndNELStoreTest, PersistReportingClients) {
  CreateStore();
  InitializeStore();
  base::Time now = base::Time::Now();
  ReportingClient endpoint =
      MakeReportingEndpoint(kOrigin1, kGroupName1, kEndpoint1);
  CachedReportingEndpointGroup group =
      MakeReportingEndpointGroup(kOrigin1, kGroupName1, now);

  store_->AddReportingEndpoint(endpoint);
  store_->AddReportingEndpointGroup(group);

  // Close and reopen the database.
  DestroyStore();
  CreateStore();

  // Load the stored clients.
  std::vector<ReportingClient> endpoints;
  std::vector<CachedReportingEndpointGroup> groups;
  LoadReportingClients(&endpoints, &groups);
  ASSERT_EQ(1u, endpoints.size());
  EXPECT_EQ(endpoint.group_key.origin, endpoints[0].group_key.origin);
  EXPECT_EQ(endpoint.group_key.group_name, endpoints[0].group_key.group_name);
  EXPECT_EQ(endpoint.info.url, endpoints[0].info.url);
  EXPECT_EQ(endpoint.info.priority, endpoints[0].info.priority);
  EXPECT_EQ(endpoint.info.weight, endpoints[0].info.weight);
  ASSERT_EQ(1u, groups.size());
  EXPECT_EQ(group.group_key.origin, groups[0].group_key.origin);
  EXPECT_EQ(group.group_key.group_name, groups[0].group_key.group_name);
  EXPECT_EQ(group.include_subdomains, groups[0].include_subdomains);
  EXPECT_TRUE(WithinOneMicrosecond(group.expires, groups[0].expires));
  EXPECT_TRUE(WithinOneMicrosecond(group.last_used, groups[0].last_used));
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       UpdateReportingEndpointGroupAccessTime) {
  CreateStore();
  InitializeStore();
  base::Time now = base::Time::Now();
  CachedReportingEndpointGroup group =
      MakeReportingEndpointGroup(kOrigin1, kGroupName1, now);

  store_->AddReportingEndpointGroup(group);

  group.last_used = now + base::TimeDelta::FromDays(1);
  store_->UpdateReportingEndpointGroupAccessTime(group);

  // Close and reopen the database.
  DestroyStore();
  CreateStore();

  std::vector<ReportingClient> endpoints;
  std::vector<CachedReportingEndpointGroup> groups;
  LoadReportingClients(&endpoints, &groups);
  ASSERT_EQ(1u, groups.size());
  EXPECT_EQ(group.group_key.origin, groups[0].group_key.origin);
  EXPECT_EQ(group.group_key.group_name, groups[0].group_key.group_name);
  EXPECT_TRUE(WithinOneMicrosecond(group.last_used, groups[0].last_used));
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       UpdateReportingEndpointDetails) {
  CreateStore();
  InitializeStore();
  ReportingClient endpoint =
      MakeReportingEndpoint(kOrigin1, kGroupName1, kEndpoint1);

  store_->AddReportingEndpoint(endpoint);

  endpoint.info.priority = 10;
  endpoint.info.weight = 10;
  store_->UpdateReportingEndpointDetails(endpoint);

  // Close and reopen the database.
  DestroyStore();
  CreateStore();

  std::vector<ReportingClient> endpoints;
  std::vector<CachedReportingEndpointGroup> groups;
  LoadReportingClients(&endpoints, &groups);
  ASSERT_EQ(1u, endpoints.size());
  EXPECT_EQ(endpoint.group_key.origin, endpoints[0].group_key.origin);
  EXPECT_EQ(endpoint.group_key.group_name, endpoints[0].group_key.group_name);
  EXPECT_EQ(endpoint.info.url, endpoints[0].info.url);
  EXPECT_EQ(endpoint.info.priority, endpoints[0].info.priority);
  EXPECT_EQ(endpoint.info.weight, endpoints[0].info.weight);
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       UpdateReportingEndpointGroupDetails) {
  CreateStore();
  InitializeStore();
  base::Time now = base::Time::Now();
  CachedReportingEndpointGroup group = MakeReportingEndpointGroup(
      kOrigin1, kGroupName1, now, OriginSubdomains::EXCLUDE, kExpires);

  store_->AddReportingEndpointGroup(group);

  group.last_used = now + base::TimeDelta::FromDays(1);
  group.expires = kExpires + base::TimeDelta::FromDays(1);
  group.include_subdomains = OriginSubdomains::INCLUDE;
  store_->UpdateReportingEndpointGroupDetails(group);

  // Close and reopen the database.
  DestroyStore();
  CreateStore();

  std::vector<ReportingClient> endpoints;
  std::vector<CachedReportingEndpointGroup> groups;
  LoadReportingClients(&endpoints, &groups);
  ASSERT_EQ(1u, groups.size());
  EXPECT_EQ(group.group_key.origin, groups[0].group_key.origin);
  EXPECT_EQ(group.group_key.group_name, groups[0].group_key.group_name);
  EXPECT_EQ(group.include_subdomains, groups[0].include_subdomains);
  EXPECT_TRUE(WithinOneMicrosecond(group.expires, groups[0].expires));
  EXPECT_TRUE(WithinOneMicrosecond(group.last_used, groups[0].last_used));
}

TEST_F(SQLitePersistentReportingAndNELStoreTest, DeleteReportingEndpoint) {
  CreateStore();
  InitializeStore();
  ReportingClient endpoint1 =
      MakeReportingEndpoint(kOrigin1, kGroupName1, kEndpoint1);
  ReportingClient endpoint2 =
      MakeReportingEndpoint(kOrigin2, kGroupName2, kEndpoint2);

  store_->AddReportingEndpoint(endpoint1);
  store_->AddReportingEndpoint(endpoint2);

  store_->DeleteReportingEndpoint(endpoint1);

  // Close and reopen the database.
  DestroyStore();
  CreateStore();

  std::vector<ReportingClient> endpoints;
  std::vector<CachedReportingEndpointGroup> groups;
  LoadReportingClients(&endpoints, &groups);
  ASSERT_EQ(1u, endpoints.size());
  EXPECT_EQ(endpoint2.info.url, endpoints[0].info.url);

  store_->DeleteReportingEndpoint(endpoint2);
  DestroyStore();
  CreateStore();

  endpoints.clear();
  LoadReportingClients(&endpoints, &groups);
  EXPECT_EQ(0u, endpoints.size());
}

TEST_F(SQLitePersistentReportingAndNELStoreTest, DeleteReportingEndpointGroup) {
  CreateStore();
  InitializeStore();
  base::Time now = base::Time::Now();
  CachedReportingEndpointGroup group1 =
      MakeReportingEndpointGroup(kOrigin1, kGroupName1, now);
  CachedReportingEndpointGroup group2 =
      MakeReportingEndpointGroup(kOrigin2, kGroupName2, now);

  store_->AddReportingEndpointGroup(group1);
  store_->AddReportingEndpointGroup(group2);

  store_->DeleteReportingEndpointGroup(group1);

  // Close and reopen the database.
  DestroyStore();
  CreateStore();

  std::vector<ReportingClient> endpoints;
  std::vector<CachedReportingEndpointGroup> groups;
  LoadReportingClients(&endpoints, &groups);
  ASSERT_EQ(1u, groups.size());
  EXPECT_EQ(group2.group_key.group_name, groups[0].group_key.group_name);

  store_->DeleteReportingEndpointGroup(group2);
  DestroyStore();
  CreateStore();

  groups.clear();
  LoadReportingClients(&endpoints, &groups);
  EXPECT_EQ(0u, groups.size());
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       ReportingEndpointUniquenessConstraint) {
  CreateStore();
  InitializeStore();
  ReportingClient endpoint1 = MakeReportingEndpoint(
      kOrigin1, kGroupName1, kEndpoint1, 1 /* priority */, 1 /* weight */);
  ReportingClient endpoint2 = MakeReportingEndpoint(
      kOrigin1, kGroupName1, kEndpoint1, 2 /* priority */, 2 /* weight */);

  store_->AddReportingEndpoint(endpoint1);
  // Adding an endpoint with the same origin, group name, and url should trigger
  // a warning and fail to execute.
  store_->AddReportingEndpoint(endpoint2);

  DestroyStore();
  CreateStore();

  std::vector<ReportingClient> endpoints;
  std::vector<CachedReportingEndpointGroup> groups;
  LoadReportingClients(&endpoints, &groups);
  // Only the first endpoint we added should be in the store.
  ASSERT_EQ(1u, endpoints.size());
  EXPECT_EQ(endpoint1.group_key.origin, endpoints[0].group_key.origin);
  EXPECT_EQ(endpoint1.group_key.group_name, endpoints[0].group_key.group_name);
  EXPECT_EQ(endpoint1.info.url, endpoints[0].info.url);
  EXPECT_EQ(endpoint1.info.priority, endpoints[0].info.priority);
  EXPECT_EQ(endpoint1.info.weight, endpoints[0].info.weight);
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       ReportingEndpointGroupUniquenessConstraint) {
  CreateStore();
  InitializeStore();
  base::Time now = base::Time::Now();
  CachedReportingEndpointGroup group1 =
      MakeReportingEndpointGroup(kOrigin1, kGroupName1, now);
  base::Time time2 = now + base::TimeDelta::FromDays(7);
  CachedReportingEndpointGroup group2 =
      MakeReportingEndpointGroup(kOrigin1, kGroupName1, time2);
  LOG(INFO) << "foo";

  store_->AddReportingEndpointGroup(group1);
  // Adding an endpoint group with the same origin and group name should trigger
  // a warning and fail to execute.
  store_->AddReportingEndpointGroup(group2);

  DestroyStore();
  CreateStore();

  std::vector<ReportingClient> endpoints;
  std::vector<CachedReportingEndpointGroup> groups;
  LoadReportingClients(&endpoints, &groups);
  // Only the first group we added should be in the store.
  ASSERT_EQ(1u, groups.size());
  EXPECT_EQ(group1.group_key.origin, groups[0].group_key.origin);
  EXPECT_EQ(group1.group_key.group_name, groups[0].group_key.group_name);
  EXPECT_EQ(group1.include_subdomains, groups[0].include_subdomains);
  EXPECT_TRUE(WithinOneMicrosecond(group1.expires, groups[0].expires));
  EXPECT_TRUE(WithinOneMicrosecond(group1.last_used, groups[0].last_used));
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       CoalesceReportingEndpointOperations) {
  ReportingClient endpoint =
      MakeReportingEndpoint(kOrigin1, kGroupName1, kEndpoint1);

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  for (const TestCase& testcase : kCoalescingTestcases) {
    CreateStore();
    base::RunLoop run_loop;
    store_->LoadReportingClients(base::BindLambdaForTesting(
        [&](std::vector<ReportingClient>,
            std::vector<CachedReportingEndpointGroup>) { run_loop.Quit(); }));
    run_loop.Run();

    // Wedge the background thread to make sure it doesn't start consuming the
    // queue.
    background_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SQLitePersistentReportingAndNELStoreTest::WaitOnEvent,
                       base::Unretained(this), &event));

    // Now run the ops, and check how much gets queued.
    for (const Op op : testcase.operations) {
      switch (op) {
        case Op::kAdd:
          store_->AddReportingEndpoint(endpoint);
          break;

        case Op::kDelete:
          store_->DeleteReportingEndpoint(endpoint);
          break;

        case Op::kUpdate:
          // Endpoints only have UPDATE_DETAILS, so in this case we use kUpdate
          // for that.
          store_->UpdateReportingEndpointDetails(endpoint);
          break;

        default:
          NOTREACHED();
          break;
      }
    }

    EXPECT_EQ(testcase.expected_queue_length,
              store_->GetQueueLengthForTesting());

    event.Signal();
    RunUntilIdle();
  }
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       DontCoalesceUnrelatedReportingEndpoints) {
  CreateStore();
  InitializeStore();

  ReportingClient endpoint1 =
      MakeReportingEndpoint(kOrigin1, kGroupName1, kEndpoint1);
  ReportingClient endpoint2 =
      MakeReportingEndpoint(kOrigin2, kGroupName2, kEndpoint2);

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  // Wedge the background thread to make sure it doesn't start consuming the
  // queue.
  background_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SQLitePersistentReportingAndNELStoreTest::WaitOnEvent,
                     base::Unretained(this), &event));

  // Delete on |endpoint2| should not cancel addition of unrelated |endpoint1|.
  store_->AddReportingEndpoint(endpoint1);
  store_->DeleteReportingEndpoint(endpoint2);
  EXPECT_EQ(2u, store_->GetQueueLengthForTesting());

  event.Signal();
  RunUntilIdle();
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       CoalesceReportingEndpointGroupOperations) {
  base::Time now = base::Time::Now();
  CachedReportingEndpointGroup group =
      MakeReportingEndpointGroup(kOrigin1, kGroupName1, now);

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  for (const TestCase& testcase : kCoalescingTestcases) {
    CreateStore();
    base::RunLoop run_loop;
    store_->LoadReportingClients(base::BindLambdaForTesting(
        [&](std::vector<ReportingClient>,
            std::vector<CachedReportingEndpointGroup>) { run_loop.Quit(); }));
    run_loop.Run();

    // Wedge the background thread to make sure it doesn't start consuming the
    // queue.
    background_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SQLitePersistentReportingAndNELStoreTest::WaitOnEvent,
                       base::Unretained(this), &event));

    // Now run the ops, and check how much gets queued.
    for (const Op op : testcase.operations) {
      switch (op) {
        case Op::kAdd:
          store_->AddReportingEndpointGroup(group);
          break;

        case Op::kDelete:
          store_->DeleteReportingEndpointGroup(group);
          break;

        case Op::kUpdate:
          store_->UpdateReportingEndpointGroupAccessTime(group);
          break;

        default:
          NOTREACHED();
          break;
      }
    }

    EXPECT_EQ(testcase.expected_queue_length,
              store_->GetQueueLengthForTesting());

    event.Signal();
    RunUntilIdle();
  }

  // Additional test cases for UPDATE_DETAILS.
  for (const TestCase& testcase : kCoalescingTestcasesForUpdateDetails) {
    CreateStore();
    base::RunLoop run_loop;
    store_->LoadReportingClients(base::BindLambdaForTesting(
        [&](std::vector<ReportingClient>,
            std::vector<CachedReportingEndpointGroup>) { run_loop.Quit(); }));
    run_loop.Run();

    // Wedge the background thread to make sure it doesn't start consuming the
    // queue.
    background_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SQLitePersistentReportingAndNELStoreTest::WaitOnEvent,
                       base::Unretained(this), &event));

    // Now run the ops, and check how much gets queued.
    for (const Op op : testcase.operations) {
      switch (op) {
        case Op::kAdd:
          store_->AddReportingEndpointGroup(group);
          break;

        case Op::kDelete:
          store_->DeleteReportingEndpointGroup(group);
          break;

        case Op::kUpdate:
          store_->UpdateReportingEndpointGroupAccessTime(group);
          break;

        case Op::kUpdateDetails:
          store_->UpdateReportingEndpointGroupDetails(group);
          break;
      }
    }

    EXPECT_EQ(testcase.expected_queue_length,
              store_->GetQueueLengthForTesting());

    event.Signal();
    RunUntilIdle();
  }
}

TEST_F(SQLitePersistentReportingAndNELStoreTest,
       DontCoalesceUnrelatedReportingEndpointGroups) {
  CreateStore();
  InitializeStore();

  base::Time now = base::Time::Now();
  CachedReportingEndpointGroup group1 =
      MakeReportingEndpointGroup(kOrigin1, kGroupName1, now);
  CachedReportingEndpointGroup group2 =
      MakeReportingEndpointGroup(kOrigin2, kGroupName2, now);

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  // Wedge the background thread to make sure it doesn't start consuming the
  // queue.
  background_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SQLitePersistentReportingAndNELStoreTest::WaitOnEvent,
                     base::Unretained(this), &event));

  // Delete on |group2| should not cancel addition of unrelated |group2|.
  store_->AddReportingEndpointGroup(group1);
  store_->DeleteReportingEndpointGroup(group2);
  EXPECT_EQ(2u, store_->GetQueueLengthForTesting());

  event.Signal();
  RunUntilIdle();
}

}  // namespace net
