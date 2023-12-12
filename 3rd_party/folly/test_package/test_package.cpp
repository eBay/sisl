#include <cstdlib>
#include <iostream>
#include <utility>
#include <folly/Format.h>
#include <folly/futures/Future.h>
#include <folly/executors/ThreadedExecutor.h>
#include <folly/Uri.h>
#include <folly/FBString.h>
#if FOLLY_HAVE_ELF
#include <folly/experimental/symbolizer/Elf.h>
#endif

int main() {
    folly::ThreadedExecutor executor;
    auto [promise, future] = folly::makePromiseContract< folly::fbstring >(&executor);
    auto unit = std::move(future).thenValue([](auto const value) {
        const folly::Uri uri(value);
        std::cout << "The authority from " << value << " is " << uri.authority() << std::endl;
    });
    promise.setValue("https://github.com/bincrafters");
    std::move(unit).get();
#if FOLLY_HAVE_ELF
    folly::symbolizer::ElfFile elffile;
#endif
    return EXIT_SUCCESS;
}
