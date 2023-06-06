#include <cereal/archives/json.hpp>
#include <cereal/types/vector.hpp>

#include "logger.h"

int main(int argc, char** argv) {
  //cereal::JSONOutputArchive archive(std::cout);
  //std::vector<int> a = { 1, 2, 3 };
  //archive(CEREAL_NVP(a), CEREAL_NVP(a));
  LOGGER.add_stream(std::cout);
  LOGGER.log_info("Hello {}", "cym");
  return 0;
}
