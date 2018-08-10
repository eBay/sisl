#include <glog/logging.h>
#include <gtest/gtest.h>

#include "metrics_monitor.hpp"

#include <string>
#include <memory>

class ConcreteMetricsResult: public monitor::MetricsResult {
 public:
  ConcreteMetricsResult() {
    
  }
  
  ~ConcreteMetricsResult() {
    
  }
  
  void publish() override {
    LOG(INFO) << " do the publishing";
  }
};

class SimpleCallBackTestClass{
 public:
  static std::unique_ptr<monitor::MetricsResult>  callback() {
    LOG(INFO) << "making call from test class";
    auto result = std::make_unique<ConcreteMetricsResult>();
    return (std::move(result));
  }
};


TEST (CollectionCallback, singleCallBack) {
  //the call exposes the reference.
  monitor::MetricsMonitor::Instance().RegisterCollectionCallback(SimpleCallBackTestClass::callback);
  monitor::MetricsCollectionCallback callback = monitor::MetricsMonitor::Instance().GetCollectionCallback();
  std::unique_ptr<monitor::MetricsResult> result = callback();
}

// Call RUN_ALL_TESTS() in main()
int main (int argc, char ** argv) {
  //with main, we can attach some google test related hooks.
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler() ;

  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}


