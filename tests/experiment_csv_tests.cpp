#include "app/experiment_csv.hpp"

#include <omp.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace {
using namespace conway;
using namespace conway::app;
namespace fs=std::filesystem;
int failures=0;
void check(bool value,const char* label) {
    if(!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
template<class Exception,class Function> void rejects(Function action,const char* label) {
    try { action(); check(false,label); } catch(const Exception&) {} catch(...) { check(false,label); }
}
struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};
struct BrokenBuffer : std::streambuf {
    std::streamsize xsputn(const char*,std::streamsize) override { return 0; }
    int_type overflow(int_type) override { return traits_type::eof(); }
};
struct BrokenFlush : std::stringbuf { int sync() override { return -1; } };
std::string read(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("Cannot read test CSV");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
const MetadataField& field(const ExperimentMetadata& metadata,std::string_view key) {
    const auto found=std::find_if(metadata.fields.begin(),metadata.fields.end(),[&](const auto& f){return f.key==key;});
    if(found==metadata.fields.end()) throw std::runtime_error("Missing metadata field: "+std::string(key));
    return *found;
}
void refresh(ExperimentResult& result) { result.summary=summarizeExperimentSamples(result.samples); }
ExperimentResult comparisonFixture(const InputData& input) {
    auto result=runExperiment(input,{2,0,2,2});
    const double values[]{1.2345678901234567,0.12345678901234566,0.33333333333333331,1.0};
    for(std::size_t i=0;i<result.samples.size();++i) result.samples[i].measurement.elapsedMs=values[i];
    refresh(result);
    return result;
}
ScalingResult scalingFixture(const input::GeneratedInput& input) {
    auto result=runScalingStudy(input,{2,0,2,{1,3}});
    const double values[]{8.125,2.125,1.0625,4.125,2.0625,10.125};
    for(std::size_t i=0;i<result.samples.size();++i) result.samples[i].execution.measurement.elapsedMs=values[i];
    result.samples[1].execution.measurement.equivalent=false;
    result.samples[2].execution.measurement.minEffectiveThreads=2;
    result.samples[2].execution.measurement.maxEffectiveThreads=3;
    result.summary=summarizeScalingSamples(result.samples,result.config);
    return result;
}
ExperimentContext comparisonContext() {
    return captureExperimentContext({"comparación,\"A\"\nα", "patrón,\"uno\"\nsegunda línea",
        {{"note","synthetic fixture, \"quoted\"\r\nsegunda línea ñ"}}});
}
ExperimentContext generatedContext(std::string id) {
    return captureExperimentContext({std::move(id),std::nullopt,{{"note","synthetic fixture; not official measurements"}}});
}
void fixtures(const fs::path& destination,const std::string& root) {
    if(!fs::is_directory(destination)) throw std::runtime_error("Fixture parent must exist");
    const auto context=comparisonContext();
    const auto input=loadInput(root+"/patterns/blinker.txt");
    auto comparison=comparisonFixture(input);
    exportExperimentCsv(comparison,context,destination/"comparison");
    const auto scalingContext=generatedContext("escalabilidad-ñ");
    const auto generated=input::generateInput({5,3,3000,0});
    const auto scaling=scalingFixture(generated);
    exportExperimentCsv(scaling,scalingContext,destination/"scaling");
    for(auto& sample:comparison.samples) sample.measurement.elapsedMs=0;
    refresh(comparison);
    exportExperimentCsv(comparison,generatedContext("zero-times"),destination/"zero");
    comparison.samples[1].measurement.elapsedMs=std::numeric_limits<double>::quiet_NaN();
    refresh(comparison);
    exportExperimentCsv(comparison,generatedContext("invalid-time"),destination/"invalid");
}
} // namespace

int runTests(int argc,char** argv) {
    if(argc<2) return 2;
    if(argc==4 && std::string(argv[2])=="--fixture") {
        const auto previous=std::locale::global(std::locale(std::locale::classic(),new CommaDecimal));
        fixtures(argv[3],argv[1]); std::locale::global(previous);
        return 0;
    }
    if(argc==4 && std::string(argv[2])=="--example") {
        const fs::path directory=argv[3];
        const auto context=captureExperimentContext({"example-comparison","patterns/blinker.txt",
            {{"note","Small functional example; not official performance measurements"}}});
        const auto input=loadInput(std::string(argv[1])+"/patterns/blinker.txt");
        const auto comparison=runExperiment(input,{2,1,3,2});
        exportExperimentCsv(comparison,context,directory/"comparison");
        const auto generated=input::generateInput({7,5,3000,42});
        const auto scalingContext=captureExperimentContext({"example-scaling",std::nullopt,
            {{"note","Small functional example; not official performance measurements"}}});
        const auto scaling=runScalingStudy(generated,{2,1,2,{1,2}});
        exportExperimentCsv(scaling,scalingContext,directory/"scaling");
        std::cout<<"PASS: small real-result export to "<<directory<<'\n';
        return 0;
    }
    const auto context=comparisonContext(); // Before execution, not generated during export.
    const auto input=loadInput(std::string(argv[1])+"/patterns/blinker.txt");
    const auto result=comparisonFixture(input);
    const auto metadata=describeExperiment(context,result);
    check(metadata.studyId==context.studyId && metadata.experimentType=="comparison", "study binding and experiment type");
    check(field(metadata,"input.origin").value=="pattern" && field(metadata,"input.pattern_name").source==MetadataSource::UserProvided,
          "pattern provenance explicitly supplied by caller");
    check(!field(metadata,"input.seed").value && field(metadata,"input.seed").source==MetadataSource::Unavailable,
          "unavailable metadata never replaced by zero");
    check(field(metadata,"execution.context_captured_at_utc").value->ends_with("Z")
          && field(metadata,"protocol.version").value==kExperimentalProtocolVersion,
          "UTC context timestamp and versioned protocol");
    check(field(metadata,"hardware.logical_processors_visible").source==MetadataSource::RuntimeObserved
          && !field(metadata,"openmp.runtime_version").value
          && field(metadata,"openmp.specification_date").detail.find("NOT")!=std::string::npos,
          "observed logical processors and honest OpenMP version availability");
    check(field(metadata,"build.git_state").source==MetadataSource::BuildCaptured
          || field(metadata,"build.git_state").source==MetadataSource::Unavailable,
          "Git is build-time evidence or explicitly unavailable");
    const auto generated=input::generateInput({5,3,3000,0});
    const auto genContext=generatedContext("generated-test");
    const auto scaling=scalingFixture(generated);
    const auto genMeta=describeExperiment(genContext,scaling);
    check(field(genMeta,"input.seed").value=="0" && field(genMeta,"input.origin").value=="generator"
          && field(genMeta,"input.requested_density_per_10000").value=="3000"
          && field(genMeta,"input.generator_version").value==input::kGeneratorVersion,
          "generated provenance, including seed zero");
    const auto unknown=describeExperiment(generatedContext("unknown-input"),result);
    check(!field(unknown,"input.origin").value, "raw InputData is not invented as a loaded pattern");

    std::ostringstream samples,summary,meta;
    const auto previous=std::locale::global(std::locale(std::locale::classic(),new CommaDecimal));
    samples.imbue(std::locale()); summary.imbue(std::locale()); meta.imbue(std::locale());
    const int threads=omp_get_max_threads(),dynamic=omp_get_dynamic();
    writeExperimentCsv(result,context,samples,summary,meta);
    std::locale::global(previous);
    check(samples.str().find("1.2345678901234567")!=std::string::npos
          && samples.str().find("0.12345678901234566")!=std::string::npos, "full precision and decimal dot under comma locale");
    check(meta.str().find("\"patrón,\"\"uno\"\"\nsegunda línea\"")!=std::string::npos,
          "quotes, commas, Unicode and embedded newlines escaped");
    check(threads==omp_get_max_threads() && dynamic==omp_get_dynamic(), "export does not change OpenMP context");
    const auto temp=fs::temp_directory_path()/("conway-csv-unit-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!fs::create_directory(temp)) throw std::runtime_error("Cannot create unit-test temporary directory");
    const auto paths=exportExperimentCsv(result,context,temp/"comparison");
    check(read(paths.samples)==samples.str() && read(paths.summary)==summary.str() && read(paths.metadata)==meta.str(),
          "file and stream exports are deterministic and readable");
    rejects<fs::filesystem_error>([&]{exportExperimentCsv(result,context,temp/"comparison");},"existing directory rejected without overwrite");
    check(read(paths.samples)==samples.str(),"previous export preserved");
    { std::ofstream blocked(temp/"blocked"); blocked<<"sentinel"; }
    rejects<fs::filesystem_error>([&]{exportExperimentCsv(result,context,temp/"blocked"/"out");},"non-directory parent rejected");
    rejects<fs::filesystem_error>([&]{exportExperimentCsv(result,context,temp/"missing"/"out");},"missing parent rejected");
    exportExperimentCsv(scaling,genContext,temp/"scaling");
    auto stale=result;
    stale.samples[1].measurement.equivalent=false;
    rejects<std::invalid_argument>([&]{exportExperimentCsv(stale,context,temp/"stale");},"stale valid summary cannot publish invalid observation as performance");
    check(!fs::exists(temp/"stale"),"invalid result creates no output directory");
    stale=result;
    ++stale.samples[1].measurement.maxEffectiveThreads;
    rejects<std::invalid_argument>([&]{exportExperimentCsv(stale,context,temp/"stale-team");},
                                  "stale stable-team summary cannot publish misleading efficiency");
    auto bad=context;
    bad.fields.push_back({"user.bad",std::string("\xc0\xaf"),MetadataSource::UserProvided,{}});
    rejects<std::invalid_argument>([&]{exportExperimentCsv(result,bad,temp/"bad-utf8");},"malformed UTF-8 rejected");
    check(!fs::exists(temp/"bad-utf8"),"bad encoding rejected before filesystem mutation");
    bad=context; bad.fields.push_back(bad.fields.front());
    rejects<std::invalid_argument>([&]{exportExperimentCsv(result,bad,temp/"duplicate");},"duplicate metadata rejected");
    rejects<std::invalid_argument>([&]{captureExperimentContext({"",std::nullopt,{}});},"empty study identifier rejected");
    rejects<std::invalid_argument>([&]{describeExperiment(context,scaling);},"generated input cannot carry contradictory pattern provenance");
    BrokenBuffer broken; std::ostream brokenStream(&broken); std::ostringstream normalA,normalB;
    rejects<std::ios_base::failure>([&]{writeExperimentCsv(result,context,brokenStream,normalA,normalB);},"write failure propagated");
    BrokenFlush brokenFlush; std::ostream flushStream(&brokenFlush);
    rejects<std::ios_base::failure>([&]{writeExperimentCsv(result,context,flushStream,normalA,normalB);},"flush failure propagated");
    auto zero=result;
    for(auto& sample:zero.samples) sample.measurement.elapsedMs=0;
    refresh(zero);
    std::ostringstream zeroA,zeroB,zeroC;
    writeExperimentCsv(zero,context,zeroA,zeroB,zeroC);
    check(zeroB.str().find("insufficient_clock_resolution")!=std::string::npos, "unavailable metrics retain reason instead of zero sentinel");
    auto invalid=zero; invalid.samples[1].measurement.elapsedMs=std::numeric_limits<double>::infinity(); refresh(invalid);
    std::ostringstream invalidA,invalidB,invalidC;
    writeExperimentCsv(invalid,context,invalidA,invalidB,invalidC);
    check(invalidA.str().find(",inf,non_finite,")!=std::string::npos && invalidB.str().find("unavailable_in_result")!=std::string::npos,
          "non-finite original observation retained with invalid status");
    const auto legacy=runBenchmark(input,1,1,1);
    check(legacy.equivalent && legacy.samples.size()==2 && result.summary.equivalent,
          "legacy and experimental APIs remain compatible");
    fs::remove_all(temp);
    std::cout<<(failures ? "FAIL" : "PASS")<<": metadata and CSV ("<<failures<<" failures)\n";
    return failures ? 1 : 0;
}

int main(int argc,char** argv) {
    try { return runTests(argc,argv); }
    catch(const std::exception& error) { std::cerr<<"ERROR: "<<error.what()<<'\n'; return 1; }
}
