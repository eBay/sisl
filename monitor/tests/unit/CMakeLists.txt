
## gtest is located under third-party external component
function (add_local_test targetname)
  handle_common_unit_tests (${targetname} ${targetname} ${targetname})
endfunction()

function (add_common_test targetname)
  add_local_test(${targetname})
  target_link_libraries(${targetname} numonitor ${PROMETHEUS_LIBRARY} ${PROTOBUFFER_LIBRARY} ${GLOG_LIBRARY} ${EVHTP_LIBRARY} ${EVENT_LIBRARY} pthread m)
endfunction()

add_common_test(test_metrics_counter_apis)
add_common_test(test_metrics_gauge_apis)
add_common_test(test_metrics_histogram_apis)
add_common_test(test_callbacks)

