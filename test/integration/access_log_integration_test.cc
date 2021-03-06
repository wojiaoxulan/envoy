#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/version.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class AccessLogIntegrationTest : public HttpIntegrationTest,
                                 public testing::TestWithParam<Network::Address::IpVersion> {
public:
  AccessLogIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::api::v2::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      accesslog_cluster->mutable_http2_protocol_options();
    });

    config_helper_.addConfigModifier(
        [](envoy::api::v2::filter::network::HttpConnectionManager& hcm) {
          auto* access_log = hcm.add_access_log();
          access_log->set_name("envoy.http_grpc_access_log");

          envoy::api::v2::filter::accesslog::HttpGrpcAccessLogConfig config;
          auto* common_config = config.mutable_common_config();
          common_config->set_log_name("foo");
          common_config->mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
              "accesslog");
          MessageUtil::jsonConvert(config, *access_log->mutable_config());
        });

    HttpIntegrationTest::initialize();
  }

  void waitForAccessLogConnection() {
    fake_access_log_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
  }

  void waitForAccessLogStream() {
    access_log_request_ = fake_access_log_connection_->waitForNewStream(*dispatcher_);
  }

  void waitForAccessLogRequest(const std::string& expected_request_msg_yaml) {
    envoy::api::v2::filter::accesslog::StreamAccessLogsMessage request_msg;
    access_log_request_->waitForGrpcMessage(*dispatcher_, request_msg);
    EXPECT_STREQ("POST", access_log_request_->headers().Method()->value().c_str());
    EXPECT_STREQ("/envoy.api.v2.filter.accesslog.AccessLogService/StreamAccessLogs",
                 access_log_request_->headers().Path()->value().c_str());
    EXPECT_STREQ("application/grpc", access_log_request_->headers().ContentType()->value().c_str());

    envoy::api::v2::filter::accesslog::StreamAccessLogsMessage expected_request_msg;
    MessageUtil::loadFromYaml(expected_request_msg_yaml, expected_request_msg);

    // Clear fields which are not deterministic.
    auto* log_entry = request_msg.mutable_http_logs()->mutable_log_entry(0);
    log_entry->mutable_common_properties()->clear_downstream_remote_address();
    log_entry->mutable_common_properties()->clear_downstream_local_address();
    log_entry->mutable_common_properties()->clear_start_time();
    log_entry->mutable_common_properties()->clear_time_to_last_downstream_tx_byte();
    log_entry->mutable_request()->clear_request_id();
    EXPECT_EQ(request_msg.DebugString(), expected_request_msg.DebugString());
  }

  void cleanup() {
    if (fake_access_log_connection_ != nullptr) {
      fake_access_log_connection_->close();
      fake_access_log_connection_->waitForDisconnect();
    }
  }

  FakeHttpConnectionPtr fake_access_log_connection_;
  FakeStreamPtr access_log_request_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, AccessLogIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Test a basic full access logging flow.
TEST_P(AccessLogIntegrationTest, BasicAccessLogFlow) {
  testRouterNotFound();
  waitForAccessLogConnection();
  waitForAccessLogStream();
  waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    build_version: {}
  log_name: foo
http_logs:
  log_entry:
    common_properties:
      response_flags:
        no_route_found: true
    protocol_version: HTTP11
    request:
      authority: host
      path: /notfound
      request_headers_bytes: 122
    response:
      response_code:
        value: 404
      response_headers_bytes: 54
)EOF",
                                      VersionInfo::version()));

  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/notfound", "", downstream_protocol_, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
  waitForAccessLogRequest(R"EOF(
http_logs:
  log_entry:
    common_properties:
      response_flags:
        no_route_found: true
    protocol_version: HTTP11
    request:
      authority: host
      path: /notfound
      request_headers_bytes: 122
    response:
      response_code:
        value: 404
      response_headers_bytes: 54
)EOF");

  // Send an empty response and end the stream. This should never happen but make sure nothing
  // breaks and we make a new stream on a follow up request.
  access_log_request_->startGrpcStream();
  envoy::api::v2::filter::accesslog::StreamAccessLogsResponse response_msg;
  access_log_request_->sendGrpcMessage(response_msg);
  access_log_request_->finishGrpcStream(Grpc::Status::Ok);
  test_server_->waitForGaugeEq("cluster.accesslog.upstream_rq_active", 0);

  response = IntegrationUtil::makeSingleRequest(lookupPort("http"), "GET", "/notfound", "",
                                                downstream_protocol_, version_);
  EXPECT_TRUE(response->complete());
  EXPECT_STREQ("404", response->headers().Status()->value().c_str());
  waitForAccessLogStream();
  waitForAccessLogRequest(fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
    build_version: {}
  log_name: foo
http_logs:
  log_entry:
    common_properties:
      response_flags:
        no_route_found: true
    protocol_version: HTTP11
    request:
      authority: host
      path: /notfound
      request_headers_bytes: 122
    response:
      response_code:
        value: 404
      response_headers_bytes: 54
)EOF",
                                      VersionInfo::version()));

  cleanup();
}

} // namespace
} // namespace Envoy
