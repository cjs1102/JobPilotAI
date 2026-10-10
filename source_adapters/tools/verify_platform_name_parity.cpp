// Cross-language platform-name parity probe (ExecPlan 0015A decision 4).
//
// The owner requires parity between the C++ PlatformNameRegistry and the Python
// jobpilot_sources.registry to be DEMONSTRATED, not assumed: both languages read
// one file, but the normalisation is code on each side. This tool prints the
// normalisation table, the registry size, membership and the canonical set so
// the Python test expectations can be produced from the real C++ implementation.
//
// Build and run from the repository root (opt-in; not part of the Python suite):
//
//   g++ -std=c++17 -I service/include -I backend/include \
//       -o /tmp/verify_platform_name_parity \
//       source_adapters/tools/verify_platform_name_parity.cpp \
//       service/src/job/job_quality.cpp
//   cd <repository root> && /tmp/verify_platform_name_parity
//
// Output shape (one record per line, '|' separated):
//   N|<input>|<normalized>       normalization of one probe
//   SIZE|<n>                     registry size after loading the data file
//   WARNING|<text>               loader warning (empty when the file loaded)
//   C|<name>|<0|1>               registry.Contains(name)
//   F|<name>|<0|1>               Contains for a name that must not match
//   SET|<names comma separated>  canonical set, enumerated over candidates

#include "jobpilot/service/job/job_quality.h"

#include <iostream>
#include <string>
#include <vector>

using jobpilot::service::PlatformNameRegistry;

int main()
{
    const std::vector<std::string> probes = {
        " 牛客网 ", "牛客网", "牛客", "智联招聘", "中华英才网", "应届生求职网",
        "大街网", "前程无忧", "BOSS直聘", "boss直聘", "NowCoder", "nowcoder",
        "网", "招聘", "招聘网", "xx招聘网", "abc网", "某公司", "  ", "51job",
        "拉勾网", "官网", "网网", "人才网", "a招聘", "\t牛客网\n",
        "　牛客　", "牛客科技", "前程无忧网"};

    for (const std::string &probe : probes)
    {
        std::cout << "N|" << probe << "|"
                  << PlatformNameRegistry::Normalize(probe) << "\n";
    }

    const std::vector<std::string> expected = {
        "牛客", "实习僧", "国聘", "智联", "前程无忧", "中华英才", "应届生求职",
        "大街", "猎聘", "拉勾", "看准", "职友集", "nowcoder", "boss直聘",
        "51job", "lagou", "liepin", "zhaopin"};

    std::string warning;
    const PlatformNameRegistry registry =
        PlatformNameRegistry::Load(
            "source_adapters/jobpilot_sources/data/platform_names.yaml",
            &warning);

    std::cout << "SIZE|" << registry.Size() << "\n";
    std::cout << "WARNING|" << warning << "\n";

    for (const std::string &name : expected)
    {
        std::cout << "C|" << name << "|"
                  << (registry.Contains(name) ? "1" : "0") << "\n";
    }

    const std::vector<std::string> foreign = {
        "某公司", "示例科技有限公司", "example.com", "Acme Robotics",
        "牛客科技"};
    for (const std::string &name : foreign)
    {
        std::cout << "F|" << name << "|"
                  << (registry.Contains(name) ? "1" : "0") << "\n";
    }

    std::cout << "SET|";
    for (const std::string &name : expected)
    {
        if (registry.Contains(name))
        {
            std::cout << name << ",";
        }
    }
    std::cout << "\n";

    return 0;
}
