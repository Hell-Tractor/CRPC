#include <cereal/archives/json.hpp>
#include <cereal/types/vector.hpp>

int main(int argc, char** argv) {
  cereal::JSONOutputArchive archive(std::cout);
  std::vector<int> a = { 1, 2, 3 };
  archive(CEREAL_NVP(a), CEREAL_NVP(a));
  return 0;
}